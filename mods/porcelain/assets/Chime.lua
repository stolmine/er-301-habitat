local app = app
local libporcelain = require "porcelain.libporcelain"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local ModeSelector = require "spreadsheet.ModeSelector"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY

local function floatMap(min, max)
  local m = app.LinearDialMap(min, max)
  m:setSteps(1, 0.1, 0.01, 0.001)
  return m
end

local function intMap(min, max)
  local m = app.LinearDialMap(min, max)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end

local bandCountMap = intMap(2, 8)
local rotateMap = intMap(-16, 16)
local qMap = floatMap(0, 1)
local coupleMap = floatMap(0, 1)
local detuneMap = floatMap(0, 1)
local driveMap = floatMap(0, 1)
local levelMap = floatMap(0, 2)
local inputLevelMap = floatMap(0, 2)
local impGainMap = floatMap(0, 2)
local spreadMap = floatMap(0, 1)

local scaleNames = {
  [0] = "chrm", "maj", "mpent", "Mpent", "whole", "harm", "pelog", "slendr"
}

local Chime = Class {}
Chime:include(Unit)

function Chime:init(args)
  args.title = "Chime"
  args.mnemonic = "Ch"
  Unit.init(self, args)
end

function Chime:onLoadGraph(channelCount)
  local op = self:addObject("op", libporcelain.Chime())

  -- Audio input passthrough (filter-bank mode)
  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    -- Mono-only unit for v0; duplicate to both outlets if chain is stereo.
    connect(op, "Out", self, "Out2")
  end

  -- Trigger inlet
  local trig = self:addObject("trig", app.Comparator())
  trig:setTriggerMode()
  connect(trig, "Out", op, "Trigger")
  self:addMonoBranch("trig", trig, "In", trig, "Out")

  -- ParameterAdapter wiring for each user-facing Bias
  local function adapter(name, paramName, initial)
    local a = self:addObject(name, app.ParameterAdapter())
    a:hardSet("Bias", initial)
    tie(op, paramName, a, "Out")
    self:addMonoBranch(name, a, "In", a, "Out")
    return a
  end

  adapter("bandCount",  "BandCount",   6)
  adapter("scale",      "Scale",       0)
  adapter("rotate",     "Rotate",      0)
  adapter("qCtl",       "Q",           0.6)
  adapter("couple",     "Couple",      0.3)
  adapter("detune",     "Detune",      0.2)
  adapter("drive",      "Drive",       0.0)
  adapter("level",      "Level",       1.0)
  adapter("inputLevel", "InputLevel",  1.0)
  adapter("impGain",    "ImpulseGain", 1.0)
  adapter("spread",     "Spread",      0.0)
end

function Chime:onLoadViews()
  local controls = {}

  controls.trig = Gate {
    button = "trig",
    description = "Trigger",
    branch = self.branches.trig,
    comparator = self.objects.trig
  }

  controls.scale = ModeSelector {
    button = "scale",
    description = "Scale",
    branch = self.branches.scale,
    gainbias = self.objects.scale,
    range = self.objects.scale,
    biasMap = intMap(0, 7),
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 0,
    modeNames = scaleNames
  }

  controls.rotate = GainBias {
    button = "rot",
    description = "Rotate",
    branch = self.branches.rotate,
    gainbias = self.objects.rotate,
    range = self.objects.rotate,
    biasMap = rotateMap,
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 0
  }

  controls.bandCount = GainBias {
    button = "bands",
    description = "Band Count",
    branch = self.branches.bandCount,
    gainbias = self.objects.bandCount,
    range = self.objects.bandCount,
    biasMap = bandCountMap,
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 6
  }

  controls.q = GainBias {
    button = "Q",
    description = "Q",
    branch = self.branches.qCtl,
    gainbias = self.objects.qCtl,
    range = self.objects.qCtl,
    biasMap = qMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.6
  }

  controls.couple = GainBias {
    button = "coup",
    description = "Couple",
    branch = self.branches.couple,
    gainbias = self.objects.couple,
    range = self.objects.couple,
    biasMap = coupleMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.3
  }

  controls.detune = GainBias {
    button = "det",
    description = "Detune",
    branch = self.branches.detune,
    gainbias = self.objects.detune,
    range = self.objects.detune,
    biasMap = detuneMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.2
  }

  controls.drive = GainBias {
    button = "drv",
    description = "Drive",
    branch = self.branches.drive,
    gainbias = self.objects.drive,
    range = self.objects.drive,
    biasMap = driveMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.level = GainBias {
    button = "lvl",
    description = "Level",
    branch = self.branches.level,
    gainbias = self.objects.level,
    range = self.objects.level,
    biasMap = levelMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }

  controls.inputLevel = GainBias {
    button = "in",
    description = "Input Level",
    branch = self.branches.inputLevel,
    gainbias = self.objects.inputLevel,
    range = self.objects.inputLevel,
    biasMap = inputLevelMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }

  controls.impGain = GainBias {
    button = "imp",
    description = "Impulse Gain",
    branch = self.branches.impGain,
    gainbias = self.objects.impGain,
    range = self.objects.impGain,
    biasMap = impGainMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }

  controls.spread = GainBias {
    button = "sprd",
    description = "Spread",
    branch = self.branches.spread,
    gainbias = self.objects.spread,
    range = self.objects.spread,
    biasMap = spreadMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  local views = {
    expanded = {
      "trig", "scale", "rotate", "bandCount", "q",
      "couple", "detune", "spread", "impGain",
      "inputLevel", "drive", "level"
    },
    collapsed = {}
  }

  return controls, views
end

local adapterBiases = {
  "bandCount", "scale", "rotate", "qCtl", "couple", "detune",
  "drive", "level", "inputLevel", "impGain", "spread"
}

function Chime:serialize()
  local t = Unit.serialize(self)
  for _, name in ipairs(adapterBiases) do
    local obj = self.objects[name]
    if obj then
      t[name] = obj:getParameter("Bias"):target()
    end
  end
  if self.objects.trig then
    t.trigThreshold = self.objects.trig:getParameter("Threshold"):target()
  end
  return t
end

function Chime:deserialize(t)
  Unit.deserialize(self, t)
  for _, name in ipairs(adapterBiases) do
    if t[name] ~= nil and self.objects[name] then
      self.objects[name]:hardSet("Bias", t[name])
    end
  end
  if t.trigThreshold ~= nil and self.objects.trig then
    self.objects.trig:hardSet("Threshold", t.trigThreshold)
  end
  -- Refresh scale ModeSelector label so the fader name matches restored index.
  if self.controls and self.controls.scale and self.controls.scale.updateLabel then
    self.controls.scale:updateLabel()
  end
end

return Chime
