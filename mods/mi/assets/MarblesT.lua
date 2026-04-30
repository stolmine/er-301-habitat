local app = app
local libmi = require "mi.libmi"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local MenuHeader = require "Unit.MenuControl.Header"
local OptionControl = require "Unit.MenuControl.OptionControl"
local Encoder = require "Encoder"

local MarblesT = Class {}
MarblesT:include(Unit)

function MarblesT:init(args)
  args.title = "Marbles T"
  args.mnemonic = "Mt"
  Unit.init(self, args)
end

function MarblesT:onLoadGraph(channelCount)
  local op = self:addObject("op", libmi.MarblesT())

  -- Clock
  local clock = self:addObject("clock", app.Comparator())
  clock:setGateMode()
  connect(clock, "Out", op, "Clock")
  self:addMonoBranch("clock", clock, "In", clock, "Out")

  -- Reset
  local reset = self:addObject("reset", app.Comparator())
  reset:setMode(app.COMPARATOR_TRIGGER_ON_RISE)
  connect(reset, "Out", op, "Reset")
  self:addMonoBranch("reset", reset, "In", reset, "Out")

  -- Jitter
  local jitter = self:addObject("jitter", app.ParameterAdapter())
  jitter:hardSet("Bias", 0.0)
  tie(op, "Jitter", jitter, "Out")
  self:addMonoBranch("jitter", jitter, "In", jitter, "Out")

  -- Deja Vu
  local dejavu = self:addObject("dejavu", app.ParameterAdapter())
  dejavu:hardSet("Bias", 0.0)
  tie(op, "Deja Vu", dejavu, "Out")
  self:addMonoBranch("dejavu", dejavu, "In", dejavu, "Out")

  -- Length
  local length = self:addObject("length", app.ParameterAdapter())
  length:hardSet("Bias", 8.0)
  tie(op, "Length", length, "Out")
  self:addMonoBranch("length", length, "In", length, "Out")

  -- Output (T1/T2 crossfade)
  local output = self:addObject("output", app.ParameterAdapter())
  output:hardSet("Bias", 0.5)
  tie(op, "Output", output, "Out")
  self:addMonoBranch("output", output, "In", output, "Out")

  -- Output routing
  for i = 1, channelCount do
    connect(op, "Out", self, "Out"..i)
  end
end

local modelNames = {
  [0] = "Bernoulli",
  [1] = "Clusters",
  [2] = "Drums",
  [3] = "Independent",
  [4] = "Divider",
  [5] = "Three States",
  [6] = "Markov"
}

function MarblesT:onShowMenu(objects, branches)
  return {
    modelHeader = MenuHeader {
      description = "Model"
    },
    model = OptionControl {
      description = "Model",
      option = objects.op:getOption("Model"),
      choices = {"Bernoulli", "Clusters", "Drums", "Independent", "Divider", "Three States", "Markov"},
      boolean = true
    }
  }, {"modelHeader", "model"}
end

function MarblesT:onLoadViews()
  return {
    clock = Gate {
      button      = "clock",
      description = "Clock",
      branch      = self.branches.clock,
      comparator  = self.objects.clock
    },
    reset = Gate {
      button      = "reset",
      description = "Reset",
      branch      = self.branches.reset,
      comparator  = self.objects.reset
    },
    jitter = GainBias {
      button        = "jitter",
      description   = "Jitter",
      branch        = self.branches.jitter,
      gainbias      = self.objects.jitter,
      range         = self.objects.jitter,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.0
    },
    dejavu = GainBias {
      button        = "deja",
      description   = "Deja Vu",
      branch        = self.branches.dejavu,
      gainbias      = self.objects.dejavu,
      range         = self.objects.dejavu,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.0
    },
    length = GainBias {
      button        = "len",
      description   = "Length",
      branch        = self.branches.length,
      gainbias      = self.objects.length,
      range         = self.objects.length,
      biasMap       = (function()
        local map = app.LinearDialMap(1, 16)
        map:setSteps(1, 1, 1, 1)
        map:setRounding(1)
        return map
      end)(),
      biasUnits     = app.unitNone,
      biasPrecision = 0,
      initialBias   = 8
    },
    output = GainBias {
      button        = "out",
      description   = "Output",
      branch        = self.branches.output,
      gainbias      = self.objects.output,
      range         = self.objects.output,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    }
  }, {
    expanded  = { "clock", "reset", "jitter", "dejavu", "length", "output" },
    collapsed = {}
  }
end

return MarblesT
