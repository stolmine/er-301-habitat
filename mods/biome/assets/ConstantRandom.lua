local app = app
local libbiome = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"

local ConstantRandom = Class {}
ConstantRandom:include(Unit)

function ConstantRandom:init(args)
  args.title = "Constant Random"
  args.mnemonic = "CR"
  Unit.init(self, args)
end

function ConstantRandom:onLoadGraph(channelCount)
  local op = self:addObject("op", libbiome.ConstantRandom())

  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  local rate = self:addObject("rate", app.ParameterAdapter())
  rate:hardSet("Bias", 5.0)
  tie(op, "Rate", rate, "Out")
  self:addMonoBranch("rate", rate, "In", rate, "Out")

  local slew = self:addObject("slew", app.ParameterAdapter())
  slew:hardSet("Bias", 0.0)
  tie(op, "Slew", slew, "Out")
  self:addMonoBranch("slew", slew, "In", slew, "Out")

  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Bias", 1.0)
  tie(op, "Level", level, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")
end

local views = {
  expanded = { "rate", "slew", "level" },
  collapsed = {}
}

local rateMap = (function()
  local m = app.LinearDialMap(0.01, 100)
  m:setSteps(10, 1, 0.1, 0.01)
  return m
end)()

local slewMap = (function()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end)()

local levelMap = (function()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end)()

function ConstantRandom:onLoadViews(objects, branches)
  local controls = {}

  controls.rate = GainBias {
    button = "rate",
    description = "Rate",
    branch = branches.rate,
    gainbias = objects.rate,
    range = objects.rate,
    biasMap = rateMap,
    biasUnits = app.unitHertz,
    biasPrecision = 2,
    initialBias = 5.0
  }

  controls.slew = GainBias {
    button = "slew",
    description = "Slew",
    branch = branches.slew,
    gainbias = objects.slew,
    range = objects.slew,
    biasMap = slewMap,
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
    biasMap = levelMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }

  return controls, views
end

return ConstantRandom
