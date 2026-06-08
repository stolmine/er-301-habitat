local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Pitch = require "Unit.ViewControl.Pitch"
local ModeSelector = require "spreadsheet.ModeSelector"
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
  -- 5 sub-outs:
  --   1: Out    — fader-selected mix (stereo L on stereo chains, mono on mono)
  --   2: Out R  — stereo R, silent on mono chains
  --   3: LOW    — parallel L-side tap (mono regardless of chain stereo)
  --   4: CENTRE — parallel L-side tap
  --   5: HIGH   — parallel L-side tap
  --
  -- Stereo via dual mono instances (Three Sisters hardware is mono;
  -- "proper stereo" = parallel placement in rack). Per-block parallel
  -- taps are derived from the L instance; if user needs R-side LOW
  -- separately, parallel-place a second Canals on an R-only chain.
  args.channelCount = 5
  args.subOutLabels = { "Out", "Out R", "LOW", "CENTRE", "HIGH" }
  Unit.init(self, args)
end

function Canals:onLoadGraph(channelCount)
  -- Single instance — Three Sisters is mono hardware. Both stereo
  -- chain channels see the same signal; for true stereo Canals,
  -- parallel-place two units.
  local op = self:addObject("op", libspreadsheet.Canals())
  connect(self, "In1", op, "In")

  -- V/Oct
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  -- Fundamental
  local fundamental = self:addObject("fundamental", app.ParameterAdapter())
  fundamental:hardSet("Bias", 0.0)
  tie(op, "Fundamental", fundamental, "Out")
  self:addMonoBranch("fundamental", fundamental, "In", fundamental, "Out")

  -- Span
  local span = self:addObject("span", app.ParameterAdapter())
  span:hardSet("Bias", 0.25)
  tie(op, "Span", span, "Out")
  self:addMonoBranch("span", span, "In", span, "Out")

  -- Quality
  local quality = self:addObject("quality", app.ParameterAdapter())
  quality:hardSet("Bias", 0.0)
  tie(op, "Quality", quality, "Out")
  self:addMonoBranch("quality", quality, "In", quality, "Out")

  -- Output fader (drives sub-out 1+2 morph content; sub-outs 3-5
  -- always carry direct per-block taps regardless of fader position).
  local output = self:addObject("output", app.ParameterAdapter())
  output:hardSet("Bias", 0.0)
  tie(op, "Output", output, "Out")
  self:addMonoBranch("output", output, "In", output, "Out")

  -- Mode
  local mode = self:addObject("mode", app.ParameterAdapter())
  mode:hardSet("Bias", 0)
  tie(op, "Mode", mode, "Out")
  self:addMonoBranch("mode", mode, "In", mode, "Out")

  -- Wire 5 framework outlets LAST — matches JF's pattern (output
  -- connects after all params/branches set up). Prior placement
  -- (connects immediately after addObject) appeared to silently
  -- break sub-out picker subscriptions for non-primary sub-outs
  -- (Out3-5) while leaving the scope viewer working. Reordering
  -- mirrors the only known-working multi-out unit in the package.
  connect(op, "Out",    self, "Out1") -- primary: fader morph
  connect(op, "Out",    self, "Out2") -- stereo R duplicate of primary
  connect(op, "Low",    self, "Out3")
  connect(op, "Centre", self, "Out4")
  connect(op, "High",   self, "Out5")
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
