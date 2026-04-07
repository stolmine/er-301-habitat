local app = app
local libbiome = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local shapeMap = (function()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.0001)
  return m
end)()

local f0Map = (function()
  local m = app.LinearDialMap(0.1, 2000)
  m:setSteps(100, 10, 1, 0.1)
  return m
end)()

local VarishapeOsc = Class {}
VarishapeOsc:include(Unit)

function VarishapeOsc:init(args)
  args.title = "Varishape Osc"
  args.mnemonic = "VO"
  Unit.init(self, args)
end

function VarishapeOsc:onLoadGraph(channelCount)
  local op = self:addObject("op", libbiome.VarishapeOsc())

  -- Sink chain input (generator)
  local sink = self:addObject("sink", app.ConstantGain())
  sink:hardSet("Gain", 0.0)
  connect(self, "In1", sink, "In")

  -- V/Oct
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")

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
  self:addMonoBranch("sync", sync, "In", sync, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")
end

function VarishapeOsc:onLoadViews(objects, branches)
  return {
    tune = Pitch {
      button = "V/Oct",
      branch = branches.tune,
      description = "V/Oct",
      offset = objects.tune,
      range = objects.tuneRange
    },
    f0 = GainBias {
      button = "f0",
      branch = branches.f0,
      description = "Fundamental",
      gainbias = objects.f0,
      range = objects.f0,
      biasMap = f0Map,
      biasUnits = app.unitHertz,
      biasPrecision = 1,
      initialBias = 110.0
    },
    shape = GainBias {
      button = "shape",
      description = "Shape",
      branch = branches.shape,
      gainbias = objects.shape,
      range = objects.shape,
      biasMap = shapeMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    level = GainBias {
      button = "level",
      description = "Level",
      branch = branches.level,
      gainbias = objects.level,
      range = objects.levelRange,
      biasMap = Encoder.getMap("[-1,1]"),
      initialBias = 0.5
    }
  }, {
    expanded = { "tune", "f0", "shape", "level" },
    collapsed = {}
  }
end

return VarishapeOsc
