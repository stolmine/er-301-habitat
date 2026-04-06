local app = app
local libstolmine = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local Gate = require "Unit.ViewControl.Gate"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local Transport = Class {}
Transport:include(Unit)

function Transport:init(args)
  args.title = "Transport"
  args.mnemonic = "Tr"
  Unit.init(self, args)
end

function Transport:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.Transport())
  local rate = self:addObject("rate", app.ParameterAdapter())
  local runGate = self:addObject("runGate", app.Comparator())
  runGate:setToggleMode()

  rate:hardSet("Bias", 120.0)

  connect(runGate, "Out", op, "Run")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  tie(op, "Rate", rate, "Out")

  self:addMonoBranch("rate", rate, "In", rate, "Out")
  self:addMonoBranch("run", runGate, "In", runGate, "Out")
end

local bpmMap = (function()
  local m = app.LinearDialMap(1, 300)
  m:setSteps(10, 1, 0.1, 0.1)
  return m
end)()

local views = {
  expanded = { "bpm", "run" },
  collapsed = {}
}

function Transport:onLoadViews(objects, branches)
  local controls = {}

  controls.bpm = GainBias {
    button = "bpm",
    branch = branches.rate,
    description = "BPM",
    gainbias = objects.rate,
    range = objects.rate,
    biasMap = bpmMap,
    biasUnits = app.unitNone,
    biasPrecision = 1,
    initialBias = 120.0
  }

  controls.run = Gate {
    button = "run",
    branch = branches.run,
    description = "Run/Stop",
    comparator = objects.runGate
  }

  return controls, views
end

return Transport
