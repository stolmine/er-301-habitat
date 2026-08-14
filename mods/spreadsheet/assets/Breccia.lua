local app = app
local libstolmine = require "spreadsheet.libspreadsheet"
local libcore = require "core.libcore"
local Class = require "Base.Class"
local Unit = require "Unit"
local Gate = require "Unit.ViewControl.Gate"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Zoomable = require "Unit.ViewControl.Zoomable"
local OptionControl = require "Unit.MenuControl.OptionControl"
local Task = require "Unit.MenuControl.Task"
local MenuHeader = require "Unit.MenuControl.Header"
local SamplePool = require "Sample.Pool"
local SamplePoolInterface = require "Sample.Pool.Interface"
-- Plain buffer editor, not SlicingView: Breccia derives its grid from Size, so
-- there are no manual slice markers to author. Manual Loops uses the same one
-- for the same reason.
local SampleEditor = require "Sample.Editor"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY

-- Waveform view over the head. Same shape as GestureSeq's WaveView, but bound
-- to the framework's TapeHeadDisplay since our head is an od::TapeHead.
local WaveView = Class {}
WaveView:include(Zoomable)

function WaveView:init(args)
  Zoomable.init(self)
  self:setClassName("Breccia.WaveView")
  local head = args.head or app.logError("%s.init: head is missing.", self)
  local width = args.width or (4 * ply)
  self.head = head

  local graphic = app.Graphic(0, 0, width, 64)
  self.mainDisplay = app.TapeHeadDisplay(head, 0, 0, width, 64)
  graphic:addChild(self.mainDisplay)
  self:setMainCursorController(self.mainDisplay)
  self:setControlGraphic(graphic)

  for i = 1, (width // ply) do
    self:addSpotDescriptor {
      center = (i - 0.5) * ply
    }
  end

  self.subDisplay = app.HeadSubDisplay(head)
end

function WaveView:setSample(sample)
  if self.mainDisplay then
    self.mainDisplay:setChannel(0)
  end
end

local Breccia = Class {}
Breccia:include(Unit)

function Breccia:init(args)
  args.title = "Breccia"
  args.mnemonic = "Sd"
  Unit.init(self, args)
end

function Breccia:onLoadGraph(channelCount)
  local head = self:addObject("head", libstolmine.Breccia())

  -- Generator: sink the chain input so it does not leak through.
  local sink = self:addObject("sink", app.ConstantGain())
  sink:hardSet("Gain", 0.0)
  connect(self, "In1", sink, "In")

  -- Speed path, lifted from the built-in players: V/Oct through the
  -- framework's VoltPerOctave, multiplied by the Speed control, clipped.
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  local pitch = self:addObject("pitch", libcore.VoltPerOctave())
  local speed = self:addObject("speed", app.GainBias())
  speed:hardSet("Bias", 1.0)
  local speedRange = self:addObject("speedRange", app.MinMax())
  local multiply = self:addObject("multiply", app.Multiply())
  local clipper = self:addObject("clipper", libcore.Clipper())
  clipper:setMaximum(64.0)
  clipper:setMinimum(-64.0)
  connect(tune, "Out", pitch, "In")
  connect(tune, "Out", tuneRange, "In")
  connect(pitch, "Out", multiply, "Left")
  connect(speed, "Out", multiply, "Right")
  connect(speed, "Out", speedRange, "In")
  connect(multiply, "Out", clipper, "In")
  connect(clipper, "Out", head, "Speed")

  local shuffle = self:addObject("shuffle", app.Comparator())
  shuffle:setTriggerMode()
  connect(shuffle, "Out", head, "Shuffle")

  local function adapter(name, bias)
    local o = self:addObject(name, app.ParameterAdapter())
    o:hardSet("Gain", 0.0)   -- CV is opt-in catalog-wide
    o:hardSet("Bias", bias)
    return o
  end

  local size = adapter("size", 8.0)
  tie(head, "Size", size, "Out")
  local glitch = adapter("glitch", 0.0)
  tie(head, "Glitch", glitch, "Out")
  local layer = adapter("layer", 0.0)
  tie(head, "Layer", layer, "Out")
  local offset = adapter("offset", 0.0)
  tie(head, "Offset", offset, "Out")
  local world = adapter("world", 0.0)
  tie(head, "World", world, "Out")
  local level = adapter("level", 0.5)
  tie(head, "Level", level, "Out")

  connect(head, "Out", self, "Out1")
  if channelCount > 1 then
    connect(head, "Out", self, "Out2")
  end

  self:addMonoBranch("shuffle", shuffle, "In", shuffle, "Out")
  self:addMonoBranch("size", size, "In", size, "Out")
  self:addMonoBranch("glitch", glitch, "In", glitch, "Out")
  self:addMonoBranch("layer", layer, "In", layer, "Out")
  self:addMonoBranch("offset", offset, "In", offset, "Out")
  self:addMonoBranch("world", world, "In", world, "Out")
  self:addMonoBranch("tune", tune, "In", tune, "Out")
  self:addMonoBranch("speed", speed, "In", speed, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")
end

-- Sample handling, lifted from the built-in players (VariSpeed is the
-- reference) so it behaves the way users already expect.

function Breccia:setSample(sample)
  if self.sample then
    self.sample:release(self)
  end
  self.sample = sample
  if self.sample then
    self.sample:claim(self)
  end

  if sample then
    self.objects.head:setSample(sample.pSample)
  else
    self.objects.head:setSample(nil)
  end

  if self.sampleEditor then
    self.sampleEditor:setSample(sample)
  end

  self:notifyControls("setSample", sample)
end

function Breccia:showSampleEditor()
  if self.sample then
    if self.sampleEditor == nil then
      self.sampleEditor = SampleEditor(self, self.objects.head)
      self.sampleEditor:setSample(self.sample)
      self.sampleEditor:setPointerLabel("G")
    end
    self.sampleEditor:show()
  else
    local Overlay = require "Overlay"
    Overlay.flashMainMessage("You must first select a sample.")
  end
end

function Breccia:doDetachSample()
  local Overlay = require "Overlay"
  Overlay.flashMainMessage("Sample detached.")
  self:setSample(nil)
end

function Breccia:doAttachSampleFromCard()
  local task = function(sample)
    if sample then
      local Overlay = require "Overlay"
      Overlay.flashMainMessage("Attached sample: %s", sample.name)
      self:setSample(sample)
    end
  end
  local Pool = require "Sample.Pool"
  Pool.chooseFileFromCard(self.loadInfo.id, task)
end

function Breccia:doAttachSampleFromPool()
  local chooser = SamplePoolInterface(self.loadInfo.id, "choose")
  chooser:setDefaultChannelCount(self.channelCount)
  chooser:highlight(self.sample)
  local task = function(sample)
    if sample then
      local Overlay = require "Overlay"
      Overlay.flashMainMessage("Attached sample: %s", sample.name)
      self:setSample(sample)
    end
  end
  chooser:subscribe("done", task)
  chooser:show()
end

function Breccia:serialize()
  local t = Unit.serialize(self)
  local sample = self.sample
  if sample then
    t.sample = SamplePool.serializeSample(sample)
  end
  return t
end

function Breccia:deserialize(t)
  Unit.deserialize(self, t)
  if t.sample then
    local sample = SamplePool.deserializeSample(t.sample, self.chain)
    if sample then
      self:setSample(sample)
    else
      local Utils = require "Utils"
      app.logError("%s:deserialize: failed to load sample.", self)
      Utils.pp(t.sample)
    end
  end
end

function Breccia:onRemove()
  self:setSample(nil)
  Unit.onRemove(self)
end

local menu = {
  "sampleHeader",
  "selectFromCard",
  "selectFromPool",
  "detachBuffer",
  "editBuffer",
  "gridHeader",
  "sizeMode"
}

function Breccia:onShowMenu(objects, branches)
  local controls = {}

  controls.sampleHeader = MenuHeader {
    description = "Sample Operations"
  }

  controls.selectFromCard = Task {
    description = "Select from Card",
    task = function()
      self:doAttachSampleFromCard()
    end
  }

  controls.selectFromPool = Task {
    description = "Select from Pool",
    task = function()
      self:doAttachSampleFromPool()
    end
  }

  controls.detachBuffer = Task {
    description = "Detach Buffer",
    task = function()
      self:doDetachSample()
    end
  }

  controls.editBuffer = Task {
    description = "Edit Buffer",
    task = function()
      self:showSampleEditor()
    end
  }

  controls.gridHeader = MenuHeader {
    description = "Slice Grid"
  }

  controls.sizeMode = OptionControl {
    description = "Size Mode",
    option = objects.head:getOption("Size Mode"),
    choices = {
      "count",
      "length"
    },
    descriptionWidth = 2
  }

  local sub = {}
  if self.sample then
    sub[1] = {
      position = app.GRID5_LINE1,
      justify = app.justifyLeft,
      text = "Attached Sample:"
    }
    sub[2] = {
      position = app.GRID5_LINE2,
      justify = app.justifyLeft,
      text = "+ " .. self.sample:getFilenameForDisplay(24)
    }
    sub[3] = {
      position = app.GRID5_LINE3,
      justify = app.justifyLeft,
      text = "+ " .. self.sample:getDurationText()
    }
    sub[4] = {
      position = app.GRID5_LINE4,
      justify = app.justifyLeft,
      text = string.format("+ %s %s %s", self.sample:getChannelText(),
                           self.sample:getSampleRateText(),
                           self.sample:getMemorySizeText())
    }
  else
    sub[1] = {
      position = app.GRID5_LINE3,
      justify = app.justifyCenter,
      text = "No sample attached."
    }
  end

  return controls, menu, sub
end

local views = {
  expanded = { "shuffle", "pitch", "speed", "size", "layer", "glitch", "world", "offset", "level" },
  collapsed = {},
  shuffle = { "wave", "shuffle" },
  size    = { "wave", "size" },
  pitch   = { "wave", "pitch" },
  speed   = { "wave", "speed" },
  layer   = { "wave", "layer" },
  offset  = { "wave", "offset" },
  glitch  = { "wave", "glitch" },
  world   = { "wave", "world" },
  level   = { "wave", "level" }
}

-- ONE normalized control. Size Mode decides whether it addresses slice COUNT
-- or slice LENGTH, so there is never a second control sitting inert. In both
-- modes higher Size = bigger slices, so flipping the option does not reverse
-- the knob. The exponential mapping lives in the DSP.
local function sizeMap()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.025, 0.005, 0.001)
  return m
end

function Breccia:onLoadViews(objects, branches)
  local controls = {}

  controls.wave = WaveView { head = objects.head }

  controls.shuffle = Gate {
    button = "shuf",
    description = "Shuffle",
    branch = branches.shuffle,
    comparator = objects.shuffle
  }

  controls.size = GainBias {
    button = "size",
    description = "Size",
    branch = branches.size,
    gainbias = objects.size,
    range = objects.size,
    biasMap = sizeMap(),
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.5
  }

  -- Character macro. At 0 the unit is a plain shuffler; raising it gives each
  -- slice at most one of mute / stutter / crush / scrub / reverse / octave up /
  -- octave down. The pattern re-rolls on the Shuffle gate.
  controls.glitch = GainBias {
    button = "glitch",
    description = "Glitch",
    branch = branches.glitch,
    gainbias = objects.glitch,
    range = objects.glitch,
    biasMap = Encoder.getMap("[0,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  -- Concurrent slice playback. At 0 the focused slice plays alone; turning up
  -- fades in symmetric neighbours (+/-1, then +/-2, then +/-3) at the same
  -- intra-slice phase, up to 7 voices, equal-power normalized.
  controls.layer = GainBias {
    button = "layer",
    description = "Layer",
    branch = branches.layer,
    gainbias = objects.layer,
    range = objects.layer,
    biasMap = Encoder.getMap("[0,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.pitch = Pitch {
    button = "V/oct",
    description = "V/oct",
    branch = branches.tune,
    offset = objects.tune,
    range = objects.tuneRange
  }

  controls.speed = GainBias {
    button = "speed",
    description = "Speed",
    branch = branches.speed,
    gainbias = objects.speed,
    range = objects.speedRange,
    biasMap = Encoder.getMap("speed"),
    biasUnits = app.unitMultiplier,
    biasPrecision = 3,
    initialBias = 1.0
  }

  -- Global bipolar bias on every slice's effect INTENSITY, after Larets' Param
  -- Offset. Sweeps the whole pattern's severity live without changing which
  -- effect each slice got.
  controls.offset = GainBias {
    button = "offset",
    description = "FX Offset",
    branch = branches.offset,
    gainbias = objects.offset,
    range = objects.offset,
    biasMap = Encoder.getMap("[-1,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  -- Which family of effects the roll draws from. Glitch alone is
  -- one-dimensional: it scales how OFTEN an effect fires but never WHICH,
  -- so the mix is the same at every setting. World morphs across four
  -- weight sets - Rhythmic, Degraded, Diffuse, Tonal - each summing to the
  -- same total, so it changes character without changing density.
  controls.world = GainBias {
    button = "world",
    description = "World",
    branch = branches.world,
    gainbias = objects.world,
    range = objects.world,
    biasMap = Encoder.getMap("[0,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.level = GainBias {
    button = "level",
    description = "Level",
    branch = branches.level,
    gainbias = objects.level,
    range = objects.level,
    biasMap = Encoder.getMap("[0,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }

  return controls, views
end

return Breccia
