local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local OptionControl = require "Unit.MenuControl.OptionControl"
local MenuHeader = require "Unit.MenuControl.Header"
local Encoder = require "Encoder"
local VisadharaPitchControl = require "spreadsheet.VisadharaPitchControl"
local ThresholdFader = require "spreadsheet.ThresholdFader"

-- Visadhara — clean-room percussion macro voice based on the public BIA
-- technical manual. Phase 1: Skin mode skeleton — 6-voice NEON additive
-- with Spread / Harmonic / Morph / Decay / Level. Trigger fires AR
-- envelope on all 6 voices. Mode / Attack / Fold added in later phases.
--
-- See planning/visadhara-initial-pass.md.

local ply = app.SECTION_PLY

local function floatMap(min, max, c, f, ff, fff)
  local m = app.LinearDialMap(min, max)
  m:setSteps(c or 0.1, f or 0.01, ff or 0.001, fff or 0.0001)
  return m
end

local pitchMap = (function()
  local m = app.LinearDialMap(20, 2000)
  m:setSteps(100, 10, 1, 0.1)
  return m
end)()

local unitMap = floatMap(0, 1)
local bipolarMap = floatMap(-1, 1)
local modeMap = (function()
  local m = app.LinearDialMap(0, 2)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end)()

-- Octave: 3 discrete values (1=Bass, 2=Alto, 3=Tenor). Integer
-- stepping via the DialMap; text labels via addThresholdLabel on
-- the readout/fader. Same threshold table used by
-- VisadharaPitchControl's sub-readout and the expanded-view
-- ThresholdFader so the two displays stay consistent.
local octaveMap = (function()
  local m = app.LinearDialMap(1, 3)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()
local octaveLabels = {
  {1.0, "Bass"},
  {1.5, "Alto"},
  {2.5, "Tenor"}
}

local Visadhara = Class {}
Visadhara:include(Unit)

function Visadhara:init(args)
  args.title = "Visadhara"
  args.mnemonic = "Vx"
  Unit.init(self, args)
end

function Visadhara:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Visadhara())

  -- V/Oct (Helicase pattern: tune ConstantOffset → ConstantGain ×10 → op
  -- "V/Oct" so the buffer carries 0.1V/oct and C++ sees 1V/oct).
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  local voctGain = self:addObject("voctGain", app.ConstantGain())
  voctGain:hardSet("Gain", 10.0)
  connect(tune, "Out", voctGain, "In")
  connect(voctGain, "Out", op, "V/Oct")
  connect(tune, "Out", tuneRange, "In")
  self:addMonoBranch("tune", tune, "In", tuneRange, "Out")

  -- Octave selector (Bass / Alto / Tenor) — moved from config menu
  -- to V/Oct shift sub + expanded-view ply. CV-able via the
  -- ParameterAdapter (was an od::Option, now an od::Parameter).
  local octave = self:addObject("octave", app.ParameterAdapter())
  octave:hardSet("Bias", 2.0)   -- Alto default (0-octave shift)
  tie(op, "Octave", octave, "Out")
  self:addMonoBranch("octave", octave, "In", octave, "Out")

  -- Base pitch at V/Oct=0. Hidden internal default (110 Hz). User
  -- adjusts pitch via V/Oct ply; base shift exposed as a config menu
  -- option in Phase 2+.
  op:hardSet("Pitch", 110.0)

  -- Trigger
  local trig = self:addObject("trig", app.Comparator())
  trig:setGateMode()
  connect(trig, "Out", op, "Trigger")
  self:addMonoBranch("trig", trig, "In", trig, "Out")

  -- Harmonic
  local harmonic = self:addObject("harmonic", app.ParameterAdapter())
  harmonic:hardSet("Bias", 0.5)
  tie(op, "Harmonic", harmonic, "Out")
  self:addMonoBranch("harmonic", harmonic, "In", harmonic, "Out")

  -- Spread
  local spread = self:addObject("spread", app.ParameterAdapter())
  spread:hardSet("Bias", 0.0)
  tie(op, "Spread", spread, "Out")
  self:addMonoBranch("spread", spread, "In", spread, "Out")

  -- Morph
  local morph = self:addObject("morph", app.ParameterAdapter())
  morph:hardSet("Bias", 0.0)
  tie(op, "Morph", morph, "Out")
  self:addMonoBranch("morph", morph, "In", morph, "Out")

  -- Fold
  local fold = self:addObject("fold", app.ParameterAdapter())
  fold:hardSet("Bias", 0.0)
  tie(op, "Fold", fold, "Out")
  self:addMonoBranch("fold", fold, "In", fold, "Out")

  -- Attack (bipolar -1..+1: noise / instant / slow)
  local attack = self:addObject("attack", app.ParameterAdapter())
  attack:hardSet("Bias", 0.0)
  tie(op, "Attack", attack, "Out")
  self:addMonoBranch("attack", attack, "In", attack, "Out")

  -- Decay
  local decay = self:addObject("decay", app.ParameterAdapter())
  decay:hardSet("Bias", 0.5)
  tie(op, "Decay", decay, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")

  -- Level
  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Bias", 0.7)
  tie(op, "Level", level, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")

  -- Mode (CV-able). Phase 1 stub — Skin only; Phase 3+ wires the
  -- crossfade between modes.
  local mode = self:addObject("mode", app.ParameterAdapter())
  mode:hardSet("Bias", 0.0)
  tie(op, "Mode", mode, "Out")
  self:addMonoBranch("mode", mode, "In", mode, "Out")

  -- Output
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end
end

local views = {
  expanded = { "trig", "tune", "mode", "spread", "harmonic", "morph", "fold", "attack", "decay", "level" },
  collapsed = {},
  -- V/Oct ply expansion shows the octave selector alongside the tune
  -- fader (ThresholdFader with Bass / Alto / Tenor labels).
  tune = { "tune", "octave" }
}

function Visadhara:onLoadViews(objects, branches)
  local controls = {}

  controls.trig = Gate {
    button = "trig",
    description = "Trigger",
    branch = branches.trig,
    comparator = objects.trig
  }

  controls.tune = VisadharaPitchControl {
    button = "V/Oct",
    description = "V/Oct",
    branch = branches.tune,
    offset = objects.tune,
    range = objects.tuneRange,
    octaveParam = objects.octave:getParameter("Bias")
  }

  controls.octave = ThresholdFader {
    button = "oct",
    description = "Octave",
    branch = branches.octave,
    gainbias = objects.octave,
    range = objects.octave,
    biasMap = octaveMap,
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 2.0,
    thresholdLabels = octaveLabels
  }

  controls.mode = GainBias {
    button = "mode",
    description = "Mode",
    branch = branches.mode,
    gainbias = objects.mode,
    range = objects.mode,
    biasMap = modeMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.spread = GainBias {
    button = "spread",
    description = "Spread",
    branch = branches.spread,
    gainbias = objects.spread,
    range = objects.spread,
    biasMap = unitMap,
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.0
  }

  controls.harmonic = GainBias {
    button = "harm",
    description = "Harmonic",
    branch = branches.harmonic,
    gainbias = objects.harmonic,
    range = objects.harmonic,
    biasMap = unitMap,
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.5
  }

  controls.morph = GainBias {
    button = "morph",
    description = "Morph",
    branch = branches.morph,
    gainbias = objects.morph,
    range = objects.morph,
    biasMap = unitMap,
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.0
  }

  controls.fold = GainBias {
    button = "fold",
    description = "Fold",
    branch = branches.fold,
    gainbias = objects.fold,
    range = objects.fold,
    biasMap = unitMap,
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.0
  }

  controls.attack = GainBias {
    button = "attack",
    description = "Attack",
    branch = branches.attack,
    gainbias = objects.attack,
    range = objects.attack,
    biasMap = bipolarMap,
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.0
  }

  controls.decay = GainBias {
    button = "decay",
    description = "Decay",
    branch = branches.decay,
    gainbias = objects.decay,
    range = objects.decay,
    biasMap = unitMap,
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.5
  }

  controls.level = GainBias {
    button = "level",
    description = "Level",
    branch = branches.level,
    gainbias = objects.level,
    range = objects.level,
    biasMap = unitMap,
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.7
  }

  return controls, views
end

-- Octave moved out of the config menu to the V/Oct shift sub +
-- expanded-view ply (with Bass / Alto / Tenor threshold labels).
local menu = {
  "modeSnapHeader",
  "modeSnap"
}

function Visadhara:onShowMenu(objects, branches)
  local controls = {}

  controls.modeSnapHeader = MenuHeader {
    description = "Mode crossfade:"
  }

  controls.modeSnap = OptionControl {
    description = "Mode crossfade",
    option = objects.op:getOption("ModeSnap"),
    choices = { "smooth", "snap" }
  }

  return controls, menu
end

return Visadhara
