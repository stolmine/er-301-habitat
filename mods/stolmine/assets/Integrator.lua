local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local Gate = require "Unit.ViewControl.Gate"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local Integrator = Class {}
Integrator:include(Unit)

function Integrator:init(args)
  args.title = "Integrator"
  args.mnemonic = "IN"
  Unit.init(self, args)
end

function Integrator:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.Integrator())
  local rate = self:addObject("rate", app.ParameterAdapter())
  local leak = self:addObject("leak", app.ParameterAdapter())
  local reset = self:addObject("reset", app.Comparator())
  reset:setTriggerMode()

  rate:hardSet("Bias", 1.0)
  leak:hardSet("Bias", 0.0)

  connect(self, "In1", op, "In")
  connect(reset, "Out", op, "Reset")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  tie(op, "Rate", rate, "Out")
  tie(op, "Leak", leak, "Out")

  self:addMonoBranch("rate", rate, "In", rate, "Out")
  self:addMonoBranch("leak", leak, "In", leak, "Out")
  self:addMonoBranch("reset", reset, "In", reset, "Out")
end

local views = {
  expanded = { "rate", "leak", "reset" },
  collapsed = {}
}

local function rateMap()
  local m = app.LinearDialMap(0, 100)
  m:setSteps(10, 1, 0.1, 0.01)
  return m
end

local function leakMap()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.25, 0.1, 0.01, 0.001)
  return m
end

function Integrator:onLoadViews(objects, branches)
  local controls = {}

  controls.rate = GainBias {
    button = "rate",
    branch = branches.rate,
    description = "Rate",
    gainbias = objects.rate,
    range = objects.rate,
    biasMap = rateMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }

  controls.leak = GainBias {
    button = "leak",
    branch = branches.leak,
    description = "Leak",
    gainbias = objects.leak,
    range = objects.leak,
    biasMap = leakMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.reset = Gate {
    button = "reset",
    branch = branches.reset,
    description = "Reset",
    comparator = objects.reset
  }

  return controls, views
end

return Integrator
