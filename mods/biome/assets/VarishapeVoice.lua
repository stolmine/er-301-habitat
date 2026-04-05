local app = app
local libstolmine = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Encoder = require "Encoder"

local VarishapeVoice = Class {}
VarishapeVoice:include(Unit)

function VarishapeVoice:init(args)
  args.title = "Varishape Voice"
  args.mnemonic = "VV"
  Unit.init(self, args)
end

function VarishapeVoice:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.VarishapeVoice())

  -- Sink chain input (generator)
  local sink = self:addObject("sink", app.ConstantGain())
  sink:hardSet("Gain", 0.0)
  connect(self, "In1", sink, "In")

  -- V/Oct
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")

  -- Gate
  local gate = self:addObject("gate", app.Comparator())
  gate:setGateMode()
  connect(gate, "Out", op, "Gate")

  -- Sync
  local sync = self:addObject("sync", app.Comparator())
  sync:setTriggerMode()
  connect(sync, "Out", op, "Sync")

  -- Parameters
  local shape = self:addObject("shape", app.ParameterAdapter())
  shape:hardSet("Bias", 0.0)
  tie(op, "Shape", shape, "Out")

  local f0 = self:addObject("f0", app.ParameterAdapter())
  f0:hardSet("Bias", 110.0)
  tie(op, "Fundamental", f0, "Out")

  local decay = self:addObject("decay", app.ParameterAdapter())
  decay:hardSet("Bias", 0.5)
  tie(op, "Decay", decay, "Out")

  -- Output VCA with level
  local vca = self:addObject("vca", app.Multiply())
  local level = self:addObject("level", app.GainBias())
  local levelRange = self:addObject("levelRange", app.MinMax())

  connect(level, "Out", levelRange, "In")
  connect(level, "Out", vca, "Left")
  connect(op, "Out", vca, "Right")
  connect(vca, "Out", self, "Out1")
  if channelCount > 1 then
    connect(vca, "Out", self, "Out2")
  end

  self:addMonoBranch("shape", shape, "In", shape, "Out")
  self:addMonoBranch("tune", tune, "In", tune, "Out")
  self:addMonoBranch("f0", f0, "In", f0, "Out")
  self:addMonoBranch("gate", gate, "In", gate, "Out")
  self:addMonoBranch("sync", sync, "In", sync, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")
end

local views = {
  expanded = { "gate", "shape", "tune", "f0", "decay", "level" },
  collapsed = {}
}

local function shapeMap()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.0001)
  return m
end

local function f0Map()
  local m = app.LinearDialMap(0.1, 2000)
  m:setSteps(100, 10, 1, 0.1)
  return m
end

local function decayMap()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.0001)
  return m
end

function VarishapeVoice:onLoadViews(objects, branches)
  local controls = {}

  controls.shape = GainBias {
    button = "shape",
    description = "Shape",
    branch = branches.shape,
    gainbias = objects.shape,
    range = objects.shape,
    biasMap = shapeMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.tune = Pitch {
    button = "V/Oct",
    branch = branches.tune,
    description = "V/Oct",
    offset = objects.tune,
    range = objects.tuneRange
  }

  controls.f0 = GainBias {
    button = "f0",
    branch = branches.f0,
    description = "Fundamental",
    gainbias = objects.f0,
    range = objects.f0,
    biasMap = f0Map(),
    biasUnits = app.unitHertz,
    biasPrecision = 1,
    initialBias = 110.0
  }

  controls.gate = Gate {
    button = "gate",
    branch = branches.gate,
    description = "Gate",
    comparator = objects.gate
  }

  controls.decay = GainBias {
    button = "decay",
    description = "Decay",
    branch = branches.decay,
    gainbias = objects.decay,
    range = objects.decay,
    biasMap = decayMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.level = GainBias {
    button = "level",
    description = "Level",
    branch = branches.level,
    gainbias = objects.level,
    range = objects.levelRange,
    biasMap = Encoder.getMap("[-1,1]"),
    initialBias = 0.5
  }

  return controls, views
end

return VarishapeVoice
