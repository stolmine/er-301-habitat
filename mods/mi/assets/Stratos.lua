local app = app
local libmi = require "mi.libmi"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local Stratos = Class {}
Stratos:include(Unit)

function Stratos:init(args)
  args.title = "Stratos"
  args.mnemonic = "St"
  Unit.init(self, args)
end

function Stratos:onLoadGraph(channelCount)
  local op = self:addObject("op", libmi.Stratos())

  -- Audio routing
  if channelCount > 1 then
    connect(self, "In1", op, "In L")
    connect(self, "In2", op, "In R")
    connect(op, "Out L", self, "Out1")
    connect(op, "Out R", self, "Out2")
  else
    connect(self, "In1", op, "In L")
    connect(self, "In1", op, "In R")
    connect(op, "Out L", self, "Out1")
  end

  -- Amount
  local amount = self:addObject("amount", app.ParameterAdapter())
  amount:hardSet("Bias", 0.54)
  tie(op, "Amount", amount, "Out")
  self:addMonoBranch("amount", amount, "In", amount, "Out")

  -- Time
  local time = self:addObject("time", app.ParameterAdapter())
  time:hardSet("Bias", 0.98)
  tie(op, "Time", time, "Out")
  self:addMonoBranch("time", time, "In", time, "Out")

  -- Diffusion
  local diffusion = self:addObject("diffusion", app.ParameterAdapter())
  diffusion:hardSet("Bias", 0.7)
  tie(op, "Diffusion", diffusion, "Out")
  self:addMonoBranch("diffusion", diffusion, "In", diffusion, "Out")

  -- Damping
  local damping = self:addObject("damping", app.ParameterAdapter())
  damping:hardSet("Bias", 0.6)
  tie(op, "Damping", damping, "Out")
  self:addMonoBranch("damping", damping, "In", damping, "Out")

  -- Gain
  local gain = self:addObject("gain", app.ParameterAdapter())
  gain:hardSet("Bias", 0.2)
  tie(op, "Gain", gain, "Out")
  self:addMonoBranch("gain", gain, "In", gain, "Out")
end

function Stratos:onLoadViews()
  return {
    amount = GainBias {
      button        = "mix",
      description   = "Amount",
      branch        = self.branches.amount,
      gainbias      = self.objects.amount,
      range         = self.objects.amount,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.54
    },
    time = GainBias {
      button        = "time",
      description   = "Time",
      branch        = self.branches.time,
      gainbias      = self.objects.time,
      range         = self.objects.time,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.98
    },
    diffusion = GainBias {
      button        = "diff",
      description   = "Diffusion",
      branch        = self.branches.diffusion,
      gainbias      = self.objects.diffusion,
      range         = self.objects.diffusion,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.7
    },
    damping = GainBias {
      button        = "damp",
      description   = "Damping",
      branch        = self.branches.damping,
      gainbias      = self.objects.damping,
      range         = self.objects.damping,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.6
    },
    gain = GainBias {
      button        = "gain",
      description   = "Input Gain",
      branch        = self.branches.gain,
      gainbias      = self.objects.gain,
      range         = self.objects.gain,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.2
    }
  }, {
    expanded  = { "amount", "time", "diffusion", "damping", "gain" },
    collapsed = {}
  }
end

return Stratos
