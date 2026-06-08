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
  -- L instance: full set of outputs (drives Out1 + LOW/CENTRE/HIGH taps)
  local op = self:addObject("op", libspreadsheet.Canals())
  connect(self, "In1", op, "In")
  connect(op, "Out",    self, "Out1")  -- primary: fader morph (L side)
  connect(op, "Low",    self, "Out3")
  connect(op, "Centre", self, "Out4")
  connect(op, "High",   self, "Out5")

  -- R instance: created only on stereo chains. Only its main Out is
  -- wired (to Out2). Its per-block outputs are unused — sub-outs
  -- 3-5 stay L-instance taps for picker simplicity.
  local opR = nil
  if channelCount > 1 then
    opR = self:addObject("opR", libspreadsheet.Canals())
    connect(self, "In2", opR, "In")
    connect(opR, "Out", self, "Out2")
  end

  -- V/Oct (shared CV; both instances track the same pitch)
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")
  if opR then connect(tune, "Out", opR, "V/Oct") end
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  -- Fundamental (shared)
  local fundamental = self:addObject("fundamental", app.ParameterAdapter())
  fundamental:hardSet("Bias", 0.0)
  tie(op, "Fundamental", fundamental, "Out")
  if opR then tie(opR, "Fundamental", fundamental, "Out") end
  self:addMonoBranch("fundamental", fundamental, "In", fundamental, "Out")

  -- Span (shared)
  local span = self:addObject("span", app.ParameterAdapter())
  span:hardSet("Bias", 0.25)
  tie(op, "Span", span, "Out")
  if opR then tie(opR, "Span", span, "Out") end
  self:addMonoBranch("span", span, "In", span, "Out")

  -- Quality (shared)
  local quality = self:addObject("quality", app.ParameterAdapter())
  quality:hardSet("Bias", 0.0)
  tie(op, "Quality", quality, "Out")
  if opR then tie(opR, "Quality", quality, "Out") end
  self:addMonoBranch("quality", quality, "In", quality, "Out")

  -- Output fader (shared; controls Out / Out R morph content)
  local output = self:addObject("output", app.ParameterAdapter())
  output:hardSet("Bias", 0.0)
  tie(op, "Output", output, "Out")
  if opR then tie(opR, "Output", output, "Out") end
  self:addMonoBranch("output", output, "In", output, "Out")

  -- Mode (shared)
  local mode = self:addObject("mode", app.ParameterAdapter())
  mode:hardSet("Bias", 0)
  tie(op, "Mode", mode, "Out")
  if opR then tie(opR, "Mode", mode, "Out") end
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
