local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local Gate = require "Unit.ViewControl.Gate"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local Gridlock = Class {}
Gridlock:include(Unit)

function Gridlock:init(args)
  args.title = "Gridlock"
  args.mnemonic = "GL"
  Unit.init(self, args)
end

function Gridlock:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.Gridlock())

  local gate1 = self:addObject("gate1", app.Comparator())
  local gate2 = self:addObject("gate2", app.Comparator())
  local gate3 = self:addObject("gate3", app.Comparator())
  gate1:setGateMode()
  gate2:setGateMode()
  gate3:setGateMode()

  local val1 = self:addObject("val1", app.ParameterAdapter())
  local val2 = self:addObject("val2", app.ParameterAdapter())
  local val3 = self:addObject("val3", app.ParameterAdapter())
  val1:hardSet("Bias", 1.0)
  val2:hardSet("Bias", 0.0)
  val3:hardSet("Bias", -1.0)

  connect(gate1, "Out", op, "Gate1")
  connect(gate2, "Out", op, "Gate2")
  connect(gate3, "Out", op, "Gate3")

  local function tieParam(name, adapter)
    tie(op, name, adapter, "Out")
  end
  tieParam("Value1", val1)
  tieParam("Value2", val2)
  tieParam("Value3", val3)

  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  self:addMonoBranch("gate1", gate1, "In", gate1, "Out")
  self:addMonoBranch("gate2", gate2, "In", gate2, "Out")
  self:addMonoBranch("gate3", gate3, "In", gate3, "Out")
  self:addMonoBranch("val1", val1, "In", val1, "Out")
  self:addMonoBranch("val2", val2, "In", val2, "Out")
  self:addMonoBranch("val3", val3, "In", val3, "Out")
end

local views = {
  expanded = { "gate1", "gate2", "gate3", "val1", "val2", "val3" },
  collapsed = {}
}

local function valueMap()
  local m = app.LinearDialMap(-5, 5)
  m:setSteps(1, 0.1, 0.01, 0.001)
  return m
end

function Gridlock:onLoadViews(objects, branches)
  local controls = {}

  controls.gate1 = Gate {
    button = "g1",
    branch = branches.gate1,
    description = "Gate 1 (highest priority)",
    comparator = objects.gate1
  }

  controls.val1 = GainBias {
    button = "v1",
    branch = branches.val1,
    description = "Value 1",
    gainbias = objects.val1,
    range = objects.val1,
    biasMap = valueMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }

  controls.gate2 = Gate {
    button = "g2",
    branch = branches.gate2,
    description = "Gate 2",
    comparator = objects.gate2
  }

  controls.val2 = GainBias {
    button = "v2",
    branch = branches.val2,
    description = "Value 2",
    gainbias = objects.val2,
    range = objects.val2,
    biasMap = valueMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.gate3 = Gate {
    button = "g3",
    branch = branches.gate3,
    description = "Gate 3 (lowest priority)",
    comparator = objects.gate3
  }

  controls.val3 = GainBias {
    button = "v3",
    branch = branches.val3,
    description = "Value 3",
    gainbias = objects.val3,
    range = objects.val3,
    biasMap = valueMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = -1.0
  }

  return controls, views
end

return Gridlock
