-- Vitrail - dual switched-capacitor character filter.
-- Two SC filters are ALWAYS in the path. Routing picks each filter's tap type and
-- whether they cascade (A>B) or sum (A+B); Clock Src picks which clock tunes them.
-- Systemic port of the profiling POC (planning/refs/compound-dsp-voice/): aliasing
-- grit, clock combs, breathing self-oscillation all emerge from the mechanism.
local app = app
local libbiome = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local ModeSelector = require "biome.ModeSelector"
local Encoder = require "Encoder"
local MenuHeader = require "Unit.MenuControl.Header"
local OptionControl = require "Unit.MenuControl.OptionControl"

local gainMap = (function()
  local map = app.LinearDialMap(0, 8)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end)()

-- Routing: every filter-type pair, series (A>B) then parallel (A+B). Index order
-- MUST match Vitrail.cpp: [0,25) series, [25,50) parallel; within, idx = a*5+b.
local ftypes = { "LP", "BP", "HP", "AP", "N" }
local routingNames = {}
do
  local idx = 0
  for a = 1, 5 do for b = 1, 5 do routingNames[idx] = ftypes[a] .. ">" .. ftypes[b]; idx = idx + 1 end end
  for a = 1, 5 do for b = 1, 5 do routingNames[idx] = ftypes[a] .. "+" .. ftypes[b]; idx = idx + 1 end end
end

local routingMap = (function()
  local map = app.LinearDialMap(0, 49)
  map:setSteps(5, 1, 1, 1)
  map:setRounding(1)
  return map
end)()

local clkMap = (function()
  local map = app.LinearDialMap(0, 2)
  map:setSteps(1, 1, 1, 1)
  map:setRounding(1)
  return map
end)()

local clkNames = { [0] = "A", [1] = "B", [2] = "Both" }

local Vitrail = Class {}
Vitrail:include(Unit)

function Vitrail:init(args)
  args.title = "Vitrail"
  args.mnemonic = "Vt"
  Unit.init(self, args)
end

-- Wire an audio-rate modulatable inlet driven by a GainBias fader with its own branch.
local function addFader(self, op, name, inletName, defaultBias)
  local o = self:addObject(name, app.GainBias())
  -- Mod gain defaults to 0: CV modulation is opt-in (habitat convention,
  -- mod-gain-default-zero). A patched inlet does nothing until the user
  -- raises this control's gain.
  o:hardSet("Gain", 0.0)
  o:hardSet("Bias", defaultBias)
  local r = self:addObject(name .. "Range", app.MinMax())
  connect(o, "Out", r, "In")
  connect(o, "Out", op, inletName)
  self:addMonoBranch(name, o, "In", o, "Out")
  return o, r
end

-- Wire a ModeSelector-driven Parameter with its own branch.
local function addModeParam(self, op, name, paramName, defaultBias)
  local p = self:addObject(name, app.ParameterAdapter())
  p:hardSet("Bias", defaultBias)
  tie(op, paramName, p, "Out")
  self:addMonoBranch(name, p, "In", p, "Out")
  return p
end

function Vitrail:onLoadGraph(channelCount)
  local op = self:addObject("op", libbiome.Vitrail())
  connect(self, "In1", op, "In")

  addFader(self, op, "cutA", "Cutoff A", 0.5)
  addFader(self, op, "cutB", "Cutoff B", 0.5)
  addFader(self, op, "res", "Resonance", 0.2)
  addFader(self, op, "gain", "Gain", 1.0)
  addModeParam(self, op, "routing", "Routing", 0)
  addModeParam(self, op, "clkSrc", "Clock Src", 0)

  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2") -- mono core; duplicate to R on stereo chains
  end
end

function Vitrail:onLoadViews()
  return {
    routing = ModeSelector {
      button = "rout",
      description = "Routing",
      branch = self.branches.routing,
      gainbias = self.objects.routing,
      range = self.objects.routing,
      biasMap = routingMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = routingNames,
      -- 50-item list: discrete accumulate-step encoder, threshold 12
      -- (~3 detents/step) - deliberately below the habitat 16 standard
      -- because the list is long. See mod ModeSelector notes.
      discrete = true,
      discreteThreshold = 12
    },
    clkSrc = ModeSelector {
      button = "clk",
      description = "Clock Src",
      branch = self.branches.clkSrc,
      gainbias = self.objects.clkSrc,
      range = self.objects.clkSrc,
      biasMap = clkMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = clkNames,
      -- Discrete accumulate-step encoder at the habitat 16 standard.
      discrete = true,
      discreteThreshold = 16
    },
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
    expanded = { "routing", "clkSrc", "cutA", "cutB", "res", "gain" },
    collapsed = {}
  }
end

-- Aliasing stays in the menu (LO/HI switched-cap anti-alias smoothing).
function Vitrail:onShowMenu(objects, branches)
  local op = objects.op
  return {
    header = MenuHeader { description = "Vitrail" },
    alias = OptionControl {
      description = "Aliasing",
      option = op:getOption("Aliasing"),
      choices = { "LO", "HI" }
    }
  }, { "header", "alias" }
end

return Vitrail
