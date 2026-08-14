local app = app
local libstolmine = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local Gate = require "Unit.ViewControl.Gate"
local GainBias = require "Unit.ViewControl.GainBias"
local Zoomable = require "Unit.ViewControl.Zoomable"
local OptionControl = require "Unit.MenuControl.OptionControl"
local Task = require "Unit.MenuControl.Task"
local MenuHeader = require "Unit.MenuControl.Header"
local SamplePool = require "Sample.Pool"
local SamplePoolInterface = require "Sample.Pool.Interface"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY

-- Waveform view over the head. Same shape as GestureSeq's WaveView, but bound
-- to the framework's TapeHeadDisplay since our head is an od::TapeHead.
local WaveView = Class {}
WaveView:include(Zoomable)

function WaveView:init(args)
  Zoomable.init(self)
  self:setClassName("Sediment.WaveView")
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

local Sediment = Class {}
Sediment:include(Unit)

function Sediment:init(args)
  args.title = "Sediment"
  args.mnemonic = "Sd"
  Unit.init(self, args)
end

function Sediment:onLoadGraph(channelCount)
  local head = self:addObject("head", libstolmine.Sediment())

  -- Generator: sink the chain input so it does not leak through.
  local sink = self:addObject("sink", app.ConstantGain())
  sink:hardSet("Gain", 0.0)
  connect(self, "In1", sink, "In")

  local trig = self:addObject("trig", app.Comparator())
  trig:setTriggerMode()
  connect(trig, "Out", head, "Trigger")

  -- Mod gain starts at 0: CV is opt-in catalog-wide.
  local sort = self:addObject("sort", app.ParameterAdapter())
  sort:hardSet("Gain", 0.0)
  sort:hardSet("Bias", 0.0)
  tie(head, "Sort", sort, "Out")

  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Gain", 0.0)
  level:hardSet("Bias", 1.0)
  tie(head, "Level", level, "Out")

  connect(head, "Out", self, "Out1")
  if channelCount > 1 then
    connect(head, "Out", self, "Out2")
  end

  self:addMonoBranch("trig", trig, "In", trig, "Out")
  self:addMonoBranch("sort", sort, "In", sort, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")
end

-- Sample handling, lifted from the built-in players (VariSpeed is the
-- reference) so it behaves the way users already expect.

function Sediment:setSample(sample)
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

  self:notifyControls("setSample", sample)
end

function Sediment:doDetachSample()
  local Overlay = require "Overlay"
  Overlay.flashMainMessage("Sample detached.")
  self:setSample(nil)
end

function Sediment:doAttachSampleFromCard()
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

function Sediment:doAttachSampleFromPool()
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

function Sediment:serialize()
  local t = Unit.serialize(self)
  local sample = self.sample
  if sample then
    t.sample = SamplePool.serializeSample(sample)
  end
  return t
end

function Sediment:deserialize(t)
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

function Sediment:onRemove()
  self:setSample(nil)
  Unit.onRemove(self)
end

local menu = {
  "sampleHeader",
  "selectFromCard",
  "selectFromPool",
  "detachBuffer",
  "optionsHeader",
  "direction",
  "loop"
}

function Sediment:onShowMenu(objects, branches)
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

  controls.optionsHeader = MenuHeader {
    description = "Sort Options"
  }

  controls.direction = OptionControl {
    description = "Sort Direction",
    option = objects.head:getOption("Direction"),
    choices = {
      "quiet first",
      "loud first"
    }
  }

  controls.loop = OptionControl {
    description = "Play Duration",
    option = objects.head:getOption("Loop"),
    choices = {
      "once",
      "loop"
    }
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
  expanded = {
    "trigger",
    "sort",
    "level"
  },
  collapsed = {},
  trigger = {
    "wave",
    "trigger"
  },
  sort = {
    "wave",
    "sort"
  },
  level = {
    "wave",
    "level"
  }
}

function Sediment:onLoadViews(objects, branches)
  local controls = {}

  controls.wave = WaveView {
    head = objects.head
  }

  controls.trigger = Gate {
    button = "trig",
    description = "Trigger",
    branch = branches.trig,
    comparator = objects.trig
  }

  controls.sort = GainBias {
    button = "sort",
    description = "Sort",
    branch = branches.sort,
    gainbias = objects.sort,
    range = objects.sort,
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
    initialBias = 1.0
  }

  return controls, views
end

return Sediment
