local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local Gate = require "Unit.ViewControl.Gate"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local PingableScaledRandom = Class {}
PingableScaledRandom:include(Unit)

function PingableScaledRandom:init(args)
  args.title = "PSR"
  args.mnemonic = "SR"
  Unit.init(self, args)
end

function PingableScaledRandom:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.PingableScaledRandom())
  local trig = self:addObject("trig", app.Comparator())
  trig:setTriggerMode()

  local scale = self:addObject("scale", app.ParameterAdapter())
  local offset = self:addObject("offset", app.ParameterAdapter())
  local levels = self:addObject("levels", app.ParameterAdapter())
  scale:hardSet("Bias", 1.0)
  offset:hardSet("Bias", 0.0)
  levels:hardSet("Bias", 0)

  connect(trig, "Out", op, "Trigger")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  tie(op, "Scale", scale, "Out")
  tie(op, "Offset", offset, "Out")
  tie(op, "Levels", levels, "Out")

  self:addMonoBranch("trig", trig, "In", trig, "Out")
  self:addMonoBranch("scale", scale, "In", scale, "Out")
  self:addMonoBranch("offset", offset, "In", offset, "Out")
  self:addMonoBranch("levels", levels, "In", levels, "Out")
end

local views = {
  expanded = { "trig", "scale", "offset", "levels" },
  collapsed = {}
}

local function scaleMap()
  local m = app.LinearDialMap(0, 5)
  m:setSteps(1, 0.1, 0.01, 0.001)
  return m
end

local function offsetMap()
  local m = app.LinearDialMap(-5, 5)
  m:setSteps(1, 0.1, 0.01, 0.001)
  return m
end

local function levelsMap()
  local m = app.LinearDialMap(0, 128)
  m:setSteps(12, 1, 1, 1)
  m:setRounding(1)
  return m
end

function PingableScaledRandom:onLoadViews(objects, branches)
  local controls = {}

  controls.trig = Gate {
    button = "trig",
    branch = branches.trig,
    description = "Trigger",
    comparator = objects.trig
  }

  controls.scale = GainBias {
    button = "scale",
    branch = branches.scale,
    description = "Scale",
    gainbias = objects.scale,
    range = objects.scale,
    biasMap = scaleMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }

  controls.offset = GainBias {
    button = "offset",
    branch = branches.offset,
    description = "Offset",
    gainbias = objects.offset,
    range = objects.offset,
    biasMap = offsetMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.levels = GainBias {
    button = "lvls",
    branch = branches.levels,
    description = "Quant Levels",
    gainbias = objects.levels,
    range = objects.levels,
    biasMap = levelsMap(),
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 0
  }

  return controls, views
end

return PingableScaledRandom
