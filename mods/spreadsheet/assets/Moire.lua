local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

-- Spread (r): the moving-lattice detune, 0..2.
local spreadMap = (function()
  local m = app.LinearDialMap(0, 2)
  m:setSteps(0.5, 0.1, 0.01, 0.001)
  return m
end)()

local levelMap = (function()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end)()

local Moire = Class {}
Moire:include(Unit)

function Moire:init(args)
  args.title = "Moire"
  args.mnemonic = "Mo"
  Unit.init(self, args)
end

function Moire:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Moire())

  -- Generator unit: sink the chain input (framework expects a sink).
  local sink = self:addObject("sink", app.ConstantGain())
  sink:hardSet("Gain", 0.0)
  connect(self, "In1", sink, "In")

  -- V/Oct (10x scaling, Mirror/Plaits convention -> C++ does 2^voct).
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  local voctGain = self:addObject("voctGain", app.ConstantGain())
  voctGain:hardSet("Gain", 10.0)
  connect(tune, "Out", voctGain, "In")
  connect(voctGain, "Out", op, "V/Oct")
  connect(tune, "Out", tuneRange, "In")
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  -- Fundamental (base pitch Hz).
  local f0 = self:addObject("f0", app.ParameterAdapter())
  f0:hardSet("Bias", 110.0)
  tie(op, "Fundamental", f0, "Out")
  self:addMonoBranch("f0", f0, "In", f0, "Out")

  -- Spread (r) - audio-rate modulatable inlet driven by a GainBias fader (Vitrail addFader
  -- pattern). GainBias output = Bias + Gain*In; the knob sets Bias, CV patches into In.
  local spread = self:addObject("spread", app.GainBias())
  spread:hardSet("Gain", 1.0)
  spread:hardSet("Bias", 0.0)
  local spreadRange = self:addObject("spreadRange", app.MinMax())
  connect(spread, "Out", spreadRange, "In")
  connect(spread, "Out", op, "Spread")
  self:addMonoBranch("spread", spread, "In", spread, "Out")

  -- Level.
  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Bias", 0.5)
  tie(op, "Level", level, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")

  connect(op, "Out", self, "Out1")
end

function Moire:onLoadViews()
  return {
    tune = Pitch {
      button      = "V/oct",
      branch      = self.branches.tune,
      description = "V/oct",
      offset      = self.objects.tune,
      range       = self.objects.tuneRange
    },
    f0 = GainBias {
      button        = "freq",
      description   = "Fundamental",
      branch        = self.branches.f0,
      gainbias      = self.objects.f0,
      range         = self.objects.f0,
      biasMap       = Encoder.getMap("oscFreq"),
      biasUnits     = app.unitHertz,
      biasPrecision = 1,
      initialBias   = 110.0
    },
    spread = GainBias {
      button        = "spread",
      description   = "Spread (r)",
      branch        = self.branches.spread,
      gainbias      = self.objects.spread,
      range         = self.objects.spreadRange,
      biasMap       = spreadMap,
      biasPrecision = 3,
      initialBias   = 0.0
    },
    level = GainBias {
      button        = "level",
      description   = "Level",
      branch        = self.branches.level,
      gainbias      = self.objects.level,
      range         = self.objects.level,
      biasMap       = levelMap,
      biasPrecision = 2,
      initialBias   = 0.5
    }
  }, {
    expanded  = { "tune", "f0", "spread", "level" },
    collapsed = {}
  }
end

return Moire
