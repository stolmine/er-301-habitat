local app = app
local libmi = require "mi.libmi"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local MenuHeader = require "Unit.MenuControl.Header"
local OptionControl = require "Unit.MenuControl.OptionControl"
local Encoder = require "Encoder"

local MarblesX = Class {}
MarblesX:include(Unit)

function MarblesX:init(args)
  args.title = "Marbles X"
  args.mnemonic = "Mx"
  Unit.init(self, args)
end

function MarblesX:onLoadGraph(channelCount)
  local op = self:addObject("op", libmi.MarblesX())

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

  -- Spread
  local spread = self:addObject("spread", app.ParameterAdapter())
  spread:hardSet("Bias", 0.5)
  tie(op, "Spread", spread, "Out")
  self:addMonoBranch("spread", spread, "In", spread, "Out")

  -- Bias
  local bias = self:addObject("bias", app.ParameterAdapter())
  bias:hardSet("Bias", 0.5)
  tie(op, "Bias", bias, "Out")
  self:addMonoBranch("bias", bias, "In", bias, "Out")

  -- Steps
  local steps = self:addObject("steps", app.ParameterAdapter())
  steps:hardSet("Bias", 0.5)
  tie(op, "Steps", steps, "Out")
  self:addMonoBranch("steps", steps, "In", steps, "Out")

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

  -- Output (X1/X2/X3 selector)
  local output = self:addObject("output", app.ParameterAdapter())
  output:hardSet("Bias", 0.0)
  tie(op, "Output", output, "Out")
  self:addMonoBranch("output", output, "In", output, "Out")

  -- Output routing
  for i = 1, channelCount do
    connect(op, "Out", self, "Out"..i)
  end
end

function MarblesX:onShowMenu(objects, branches)
  return {
    modeHeader = MenuHeader {
      description = "Control Mode"
    },
    controlMode = OptionControl {
      description = "Control Mode",
      option = objects.op:getOption("Control Mode"),
      choices = {"Identical", "Bump", "Tilt"},
      boolean = true
    }
  }, {"modeHeader", "controlMode"}
end

function MarblesX:onLoadViews()
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
    spread = GainBias {
      button        = "sprd",
      description   = "Spread",
      branch        = self.branches.spread,
      gainbias      = self.objects.spread,
      range         = self.objects.spread,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    bias = GainBias {
      button        = "bias",
      description   = "Bias",
      branch        = self.branches.bias,
      gainbias      = self.objects.bias,
      range         = self.objects.bias,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    steps = GainBias {
      button        = "steps",
      description   = "Steps",
      branch        = self.branches.steps,
      gainbias      = self.objects.steps,
      range         = self.objects.steps,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
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
      initialBias   = 0.0
    }
  }, {
    expanded  = { "clock", "reset", "spread", "bias", "steps", "dejavu", "length", "output" },
    collapsed = {}
  }
end

return MarblesX
