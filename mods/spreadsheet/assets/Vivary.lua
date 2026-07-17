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
local Encoder = require "Encoder"

local function intMap(lo, hi, coarse)
  local map = app.LinearDialMap(lo, hi)
  map:setSteps(coarse, coarse, 1, 1)
  map:setRounding(1)
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

  local function param(name, key, default)
    local a = self:addObject(key, app.ParameterAdapter())
    a:hardSet("Bias", default)
    tie(op, name, a, "Out")
    self:addMonoBranch(key, a, "In", a, "Out")
    return a
  end

  param("Freq", "freq", 110.0)
  param("Rule", "rule", 30)
  param("Res", "res", 64)
  param("NClk", "nclk", 1)
  param("Reset", "reset", 0)
end

function Vivary:onLoadViews()
  return {
    freq = GainBias {
      button = "freq",
      description = "Frequency",
      branch = self.branches.freq,
      gainbias = self.objects.freq,
      range = self.objects.freq,
      biasMap = Encoder.getMap("oscFreq"),
      biasUnits = app.unitHertz,
      biasPrecision = 1,
      initialBias = 110.0
    },
    rule = GainBias {
      button = "rule",
      description = "Rule (0-255)",
      branch = self.branches.rule,
      gainbias = self.objects.rule,
      range = self.objects.rule,
      biasMap = intMap(0, 255, 8),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 30
    },
    res = GainBias {
      button = "res",
      description = "Resolution (cells)",
      branch = self.branches.res,
      gainbias = self.objects.res,
      range = self.objects.res,
      biasMap = intMap(2, 256, 8),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 64
    },
    nclk = GainBias {
      button = "clk",
      description = "CA clock (passes/update)",
      branch = self.branches.nclk,
      gainbias = self.objects.nclk,
      range = self.objects.nclk,
      biasMap = intMap(1, 64, 4),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 1
    },
    reset = GainBias {
      button = "rset",
      description = "Reset interval (0=off)",
      branch = self.branches.reset,
      gainbias = self.objects.reset,
      range = self.objects.reset,
      biasMap = intMap(0, 256, 8),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0
    }
  }, {
    expanded = { "freq", "rule", "res", "nclk", "reset" },
    collapsed = {}
  }
end

return Vivary
