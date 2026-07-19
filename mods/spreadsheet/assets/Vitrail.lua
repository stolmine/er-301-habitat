-- Vitrail - dual switched-capacitor character filter.
-- Systemic port of the profiling POC v5 (planning/refs/compound-dsp-voice/):
-- two SC cores on their own drifting clocks + a shared resonance loop. Aliasing
-- grit, clock combs, breathing self-oscillation, and mode reshaping all emerge.
local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local MenuHeader = require "Unit.MenuControl.Header"
local OptionControl = require "Unit.MenuControl.OptionControl"

local gainMap = (function()
  local map = app.LinearDialMap(0, 8)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end)()

local Vitrail = Class {}
Vitrail:include(Unit)

function Vitrail:init(args)
  args.title = "Vitrail"
  args.mnemonic = "Vt"
  Unit.init(self, args)
end

-- Wire an audio-rate modulatable inlet driven by a GainBias fader with its own
-- modulation branch (matches the Canals inlet pattern).
local function addFader(self, op, name, inletName, defaultBias)
  local o = self:addObject(name, app.GainBias())
  o:hardSet("Gain", 1.0)
  o:hardSet("Bias", defaultBias)
  local r = self:addObject(name .. "Range", app.MinMax())
  connect(o, "Out", r, "In")
  connect(o, "Out", op, inletName)
  self:addMonoBranch(name, o, "In", o, "Out")
  return o, r
end

function Vitrail:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Vitrail())
  connect(self, "In1", op, "In")

  addFader(self, op, "cutA", "Cutoff A", 0.5)
  addFader(self, op, "cutB", "Cutoff B", 0.5)
  addFader(self, op, "res", "Resonance", 0.2)
  addFader(self, op, "gain", "Gain", 1.0)

  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2") -- mono core; duplicate to R on stereo chains
  end
end

function Vitrail:onLoadViews()
  return {
    cutA = GainBias {
      button = "cutA",
      description = "Cutoff A",
      branch = self.branches.cutA,
      gainbias = self.objects.cutA,
      range = self.objects.cutARange,
      biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias = 0.5
    },
    cutB = GainBias {
      button = "cutB",
      description = "Cutoff B",
      branch = self.branches.cutB,
      gainbias = self.objects.cutB,
      range = self.objects.cutBRange,
      biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias = 0.5
    },
    res = GainBias {
      button = "res",
      description = "Resonance",
      branch = self.branches.res,
      gainbias = self.objects.res,
      range = self.objects.resRange,
      biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias = 0.2
    },
    gain = GainBias {
      button = "gain",
      description = "Gain",
      branch = self.branches.gain,
      gainbias = self.objects.gain,
      range = self.objects.gainRange,
      biasMap = gainMap,
      biasPrecision = 2,
      initialBias = 1.0
    }
  }, {
    expanded = { "cutA", "cutB", "res", "gain" },
    collapsed = {}
  }
end

-- Mode / clock source / aliasing are the shared discrete toggles. Mode selects the
-- output tap AND reshapes the shared feedback; clock source picks A / B / Both
-- (Both is the dual-core comb + self-osc engine); aliasing toggles the HF smoothing.
function Vitrail:onShowMenu(objects, branches)
  local op = objects.op
  return {
    header = MenuHeader { description = "Vitrail" },
    mode = OptionControl {
      description = "Mode",
      option = op:getOption("Mode"),
      choices = { "LP", "BP", "HP", "Notch", "AP", "Hidden" }
    },
    clkSrc = OptionControl {
      description = "Clock Src",
      option = op:getOption("Clock Src"),
      choices = { "A", "B", "Both" }
    },
    alias = OptionControl {
      description = "Aliasing",
      option = op:getOption("Aliasing"),
      choices = { "LO", "HI" }
    }
  }, { "header", "mode", "clkSrc", "alias" }
end

return Vitrail
