-- Vivary -- a generative noise/texture source built on a 1D elementary
-- cellular automaton (working name). The CA row is the wavetable; a read head
-- scans it at Freq and the CA advances one generation at each wavetable-pass
-- boundary (click-free) every NClk passes -- static structured tone (NClk high)
-- to aperiodic rule-structured noise (NClk=1). POC (phase 1): mono, binary
-- +/-1 cells. See planning/ca-wavetable-noise-design.md.

local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Pitch = require "Unit.ViewControl.Pitch"
local Encoder = require "Encoder"

-- All controls are normalized 0..1 (easy modulation from any unit; the C++
-- atom maps each to its real range). House coarse step 0.01.
local function normMap()
  local map = app.LinearDialMap(0, 1)
  map:setSteps(0.1, 0.01, 0.001, 0.0001)
  return map
end

local Vivary = Class {}
Vivary:include(Unit)

function Vivary:init(args)
  args.title = "Vivary"
  args.mnemonic = "Vv"
  Unit.init(self, args)
end

function Vivary:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Vivary())
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  -- V/Oct pitch input (ER-301 pitch convention).
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  local function param(name, key, default)
    local a = self:addObject(key, app.ParameterAdapter())
    a:hardSet("Bias", default)
    tie(op, name, a, "Out")
    self:addMonoBranch(key, a, "In", a, "Out")
    return a
  end

  param("Freq", "freq", 110.0)
  param("Family", "family", 0.0)
  param("Rule", "rule", 0.0)
  param("Res", "res", 0.24)
  param("Evolve", "evolve", 1.0)
  param("Reset", "reset", 0.0)
  param("Smooth", "smooth", 0.0)
  param("Overlap", "overlap", 0.0)
end

function Vivary:onLoadViews()
  return {
    tune = Pitch {
      button = "V/Oct",
      branch = self.branches.tune,
      description = "V/Oct",
      offset = self.objects.tune,
      range = self.objects.tuneRange
    },
    freq = GainBias {
      button = "f0",
      description = "Fundamental",
      branch = self.branches.freq,
      gainbias = self.objects.freq,
      range = self.objects.freq,
      biasMap = Encoder.getMap("oscFreq"),
      biasUnits = app.unitHertz,
      biasPrecision = 1,
      initialBias = 110.0
    },
    family = GainBias {
      button = "fam",
      description = "Family (chaos/struct/glider/rev/fractal)",
      branch = self.branches.family,
      gainbias = self.objects.family,
      range = self.objects.family,
      biasMap = normMap(),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    rule = GainBias {
      button = "rule",
      description = "Rule",
      branch = self.branches.rule,
      gainbias = self.objects.rule,
      range = self.objects.rule,
      biasMap = normMap(),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    res = GainBias {
      button = "res",
      description = "Resolution",
      branch = self.branches.res,
      gainbias = self.objects.res,
      range = self.objects.res,
      biasMap = normMap(),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.24
    },
    evolve = GainBias {
      button = "evo",
      description = "Evolve (static-noise)",
      branch = self.branches.evolve,
      gainbias = self.objects.evolve,
      range = self.objects.evolve,
      biasMap = normMap(),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    reset = GainBias {
      button = "rset",
      description = "Reset interval (0=off)",
      branch = self.branches.reset,
      gainbias = self.objects.reset,
      range = self.objects.reset,
      biasMap = normMap(),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    smooth = GainBias {
      button = "smth",
      description = "Smooth (harsh-soft)",
      branch = self.branches.smooth,
      gainbias = self.objects.smooth,
      range = self.objects.smooth,
      biasMap = normMap(),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    overlap = GainBias {
      button = "olap",
      description = "Overlap (grain layering)",
      branch = self.branches.overlap,
      gainbias = self.objects.overlap,
      range = self.objects.overlap,
      biasMap = normMap(),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    }
  }, {
    expanded = { "tune", "freq", "family", "rule", "res", "evolve", "reset", "smooth", "overlap" },
    collapsed = {}
  }
end

return Vivary
