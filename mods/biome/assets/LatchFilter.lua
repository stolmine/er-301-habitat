local app = app
local libstolmine = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Pitch = require "Unit.ViewControl.Pitch"
local ModeSelector = require "biome.ModeSelector"
local Encoder = require "Encoder"

local freqMap = (function()
  local map = app.LinearDialMap(-48, 48)
  map:setSteps(1, 1, 0.1, 0.01)
  return map
end)()

local modeMap = (function()
  local map = app.LinearDialMap(0, 1)
  map:setSteps(1, 1, 1, 1)
  map:setRounding(1)
  return map
end)()

local modeNames = {
  [0] = "LP",
  [1] = "HP"
}

local LatchFilter = Class {}
LatchFilter:include(Unit)

function LatchFilter:init(args)
  args.title = "Latch Filter"
  args.mnemonic = "LF"
  Unit.init(self, args)
end

function LatchFilter:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.LatchFilter())
  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    local opR = self:addObject("opR", libstolmine.LatchFilter())
    connect(self, "In2", opR, "In")
    connect(opR, "Out", self, "Out2")
  end

  -- V/Oct
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")
  if channelCount > 1 then connect(tune, "Out", self.objects.opR, "V/Oct") end
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  -- Fundamental (semitone offset)
  local fundamental = self:addObject("fundamental", app.ParameterAdapter())
  fundamental:hardSet("Bias", 0.0)
  tie(op, "Fundamental", fundamental, "Out")
  if channelCount > 1 then tie(self.objects.opR, "Fundamental", fundamental, "Out") end
  self:addMonoBranch("fundamental", fundamental, "In", fundamental, "Out")

  -- Resonance
  local resonance = self:addObject("resonance", app.ParameterAdapter())
  resonance:hardSet("Bias", 0.5)
  tie(op, "Resonance", resonance, "Out")
  if channelCount > 1 then tie(self.objects.opR, "Resonance", resonance, "Out") end
  self:addMonoBranch("resonance", resonance, "In", resonance, "Out")

  -- Mode
  local mode = self:addObject("mode", app.ParameterAdapter())
  mode:hardSet("Bias", 0)
  tie(op, "Mode", mode, "Out")
  if channelCount > 1 then tie(self.objects.opR, "Mode", mode, "Out") end
  self:addMonoBranch("mode", mode, "In", mode, "Out")
end

function LatchFilter:onLoadViews()
  return {
    tune = Pitch {
      button      = "V/oct",
      branch      = self.branches.tune,
      description = "V/oct",
      offset      = self.objects.tune,
      range       = self.objects.tuneRange
    },
    fundamental = GainBias {
      button        = "freq",
      description   = "Fundamental",
      branch        = self.branches.fundamental,
      gainbias      = self.objects.fundamental,
      range         = self.objects.fundamental,
      biasMap       = freqMap,
      biasPrecision = 1,
      initialBias   = 0.0
    },
    resonance = GainBias {
      button        = "res",
      description   = "Resonance",
      branch        = self.branches.resonance,
      gainbias      = self.objects.resonance,
      range         = self.objects.resonance,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
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
    }
  }, {
    expanded  = { "tune", "fundamental", "resonance", "mode" },
    collapsed = {}
  }
end

return LatchFilter
