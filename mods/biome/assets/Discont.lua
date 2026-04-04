local app = app
local libstolmine = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local ModeSelector = require "biome.ModeSelector"
local Encoder = require "Encoder"

local modeMap = (function()
  local map = app.LinearDialMap(0, 6)
  map:setSteps(1, 1, 1, 1)
  map:setRounding(1)
  return map
end)()

local modeNames = {
  [0] = "Fold",
  [1] = "Tanh",
  [2] = "Soft",
  [3] = "Clip",
  [4] = "Sqrt",
  [5] = "Rect",
  [6] = "Crush"
}

local Discont = Class {}
Discont:include(Unit)

function Discont:init(args)
  args.title = "94 Discont"
  args.mnemonic = "Dc"
  Unit.init(self, args)
end

function Discont:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.Discont())
  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    local opR = self:addObject("opR", libstolmine.Discont())
    connect(self, "In2", opR, "In")
    connect(opR, "Out", self, "Out2")
  end

  local mode = self:addObject("mode", app.ParameterAdapter())
  mode:hardSet("Bias", 0)
  tie(op, "Mode", mode, "Out")
  if channelCount > 1 then tie(self.objects.opR, "Mode", mode, "Out") end
  self:addMonoBranch("mode", mode, "In", mode, "Out")

  local amount = self:addObject("amount", app.ParameterAdapter())
  amount:hardSet("Bias", 1.0)
  tie(op, "Amount", amount, "Out")
  if channelCount > 1 then tie(self.objects.opR, "Amount", amount, "Out") end
  self:addMonoBranch("amount", amount, "In", amount, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 1.0)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

function Discont:onLoadViews()
  return {
    mode = ModeSelector {
      button        = "mode",
      description   = "Mode",
      branch        = self.branches.mode,
      gainbias      = self.objects.mode,
      range         = self.objects.mode,
      biasMap       = modeMap,
      biasUnits     = app.unitNone,
      biasPrecision = 0,
      initialBias   = 0,
      modeNames     = modeNames
    },
    amount = GainBias {
      button        = "amt",
      description   = "Amount",
      branch        = self.branches.amount,
      gainbias      = self.objects.amount,
      range         = self.objects.amount,
      biasMap       = Encoder.getMap("[0,10]"),
      biasPrecision = 2,
      initialBias   = 1.0
    },
    mix = GainBias {
      button        = "mix",
      description   = "Mix",
      branch        = self.branches.mix,
      gainbias      = self.objects.mix,
      range         = self.objects.mix,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 1.0
    }
  }, {
    expanded  = { "mode", "amount", "mix" },
    collapsed = {}
  }
end

return Discont
