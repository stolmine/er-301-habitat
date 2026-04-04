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
  map:setSteps(12, 1, 0.1, 0.01)
  return map
end)()

local outputMap = (function()
  local map = app.LinearDialMap(0, 3)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end)()

local outputNames = {
  [0] = "LOW",
  [1] = "CTR",
  [2] = "HIGH",
  [3] = "ALL"
}

local modeMap = (function()
  local map = app.LinearDialMap(0, 1)
  map:setSteps(1, 1, 1, 1)
  map:setRounding(1)
  return map
end)()

local modeNames = {
  [0] = "Xover",
  [1] = "Formnt"
}

local Canals = Class {}
Canals:include(Unit)

function Canals:init(args)
  args.title = "Canals"
  args.mnemonic = "Ca"
  Unit.init(self, args)
end

function Canals:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.Canals())
  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    local opR = self:addObject("opR", libstolmine.Canals())
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

  -- Fundamental
  local fundamental = self:addObject("fundamental", app.ParameterAdapter())
  fundamental:hardSet("Bias", 0.0)
  tie(op, "Fundamental", fundamental, "Out")
  if channelCount > 1 then tie(self.objects.opR, "Fundamental", fundamental, "Out") end
  self:addMonoBranch("fundamental", fundamental, "In", fundamental, "Out")

  -- Span
  local span = self:addObject("span", app.ParameterAdapter())
  span:hardSet("Bias", 0.25)
  tie(op, "Span", span, "Out")
  if channelCount > 1 then tie(self.objects.opR, "Span", span, "Out") end
  self:addMonoBranch("span", span, "In", span, "Out")

  -- Quality
  local quality = self:addObject("quality", app.ParameterAdapter())
  quality:hardSet("Bias", 0.0)
  tie(op, "Quality", quality, "Out")
  if channelCount > 1 then tie(self.objects.opR, "Quality", quality, "Out") end
  self:addMonoBranch("quality", quality, "In", quality, "Out")

  -- Output
  local output = self:addObject("output", app.ParameterAdapter())
  output:hardSet("Bias", 0.0)
  tie(op, "Output", output, "Out")
  if channelCount > 1 then tie(self.objects.opR, "Output", output, "Out") end
  self:addMonoBranch("output", output, "In", output, "Out")

  -- Mode
  local mode = self:addObject("mode", app.ParameterAdapter())
  mode:hardSet("Bias", 0)
  tie(op, "Mode", mode, "Out")
  if channelCount > 1 then tie(self.objects.opR, "Mode", mode, "Out") end
  self:addMonoBranch("mode", mode, "In", mode, "Out")
end

function Canals:onLoadViews()
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
    span = GainBias {
      button        = "span",
      description   = "Span",
      branch        = self.branches.span,
      gainbias      = self.objects.span,
      range         = self.objects.span,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.25
    },
    quality = GainBias {
      button        = "qual",
      description   = "Quality",
      branch        = self.branches.quality,
      gainbias      = self.objects.quality,
      range         = self.objects.quality,
      biasMap       = Encoder.getMap("[-1,1]"),
      biasPrecision = 2,
      initialBias   = 0.0
    },
    output = ModeSelector {
      button        = "out",
      description   = "Output",
      branch        = self.branches.output,
      gainbias      = self.objects.output,
      range         = self.objects.output,
      biasMap       = outputMap,
      biasUnits     = app.unitNone,
      biasPrecision = 1,
      initialBias   = 0.0,
      modeNames     = outputNames
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
    expanded  = { "mode", "tune", "fundamental", "span", "quality", "output" },
    collapsed = {}
  }
end

return Canals
