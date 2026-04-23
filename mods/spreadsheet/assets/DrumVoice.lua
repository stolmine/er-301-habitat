local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local DrumVoicePitchControl = require "spreadsheet.DrumVoicePitchControl"
local DrumVoiceCharacterControl = require "spreadsheet.DrumVoiceCharacterControl"
local DrumVoiceSweepControl = require "spreadsheet.DrumVoiceSweepControl"
local DrumVoiceDecayControl = require "spreadsheet.DrumVoiceDecayControl"
local DrumVoiceLevelControl = require "spreadsheet.DrumVoiceLevelControl"
local DrumVoiceRandomGateControl = require "spreadsheet.DrumVoiceRandomGateControl"
local Encoder = require "Encoder"

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local DrumVoice = Class {}
DrumVoice:include(Unit)

function DrumVoice:init(args)
  args.title = "Ngoma"
  args.mnemonic = "NG"
  Unit.init(self, args)
end

function DrumVoice:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.DrumVoice())

  local trig = self:addObject("trig", app.Comparator())
  trig:setTriggerMode()
  connect(trig, "Out", op, "Trigger")

  local xformTrig = self:addObject("xformTrig", app.Comparator())
  xformTrig:setTriggerMode()
  connect(xformTrig, "Out", op, "XformGate")

  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")

  local character  = self:addObject("character",  app.ParameterAdapter())
  local shape      = self:addObject("shape",      app.ParameterAdapter())
  local grit       = self:addObject("grit",       app.ParameterAdapter())
  local punch      = self:addObject("punch",      app.ParameterAdapter())
  local sweep      = self:addObject("sweep",      app.ParameterAdapter())
  local sweepTime  = self:addObject("sweepTime",  app.ParameterAdapter())
  local attack     = self:addObject("attack",     app.ParameterAdapter())
  local hold       = self:addObject("hold",       app.ParameterAdapter())
  local decay      = self:addObject("decay",      app.ParameterAdapter())
  local clipper    = self:addObject("clipper",    app.ParameterAdapter())
  local eq         = self:addObject("eq",         app.ParameterAdapter())
  local level      = self:addObject("level",      app.ParameterAdapter())
  local compAmt    = self:addObject("compAmt",    app.ParameterAdapter())
  local octave     = self:addObject("octave",     app.ParameterAdapter())
  local depth      = self:addObject("depth",      app.ParameterAdapter())
  local spread     = self:addObject("spread",     app.ParameterAdapter())

  character:hardSet("Bias", 0.5)
  shape:hardSet("Bias", 0.0)
  grit:hardSet("Bias", 0.0)
  punch:hardSet("Bias", 0.4)
  sweep:hardSet("Bias", 18.0)
  sweepTime:hardSet("Bias", 0.04)
  attack:hardSet("Bias", 0.0)
  hold:hardSet("Bias", 0.0)
  decay:hardSet("Bias", 0.25)
  clipper:hardSet("Bias", 0.0)
  eq:hardSet("Bias", 0.0)
  level:hardSet("Bias", 0.8)
  compAmt:hardSet("Bias", 0.0)
  octave:hardSet("Bias", 0.0)
  depth:hardSet("Bias", 0.3)
  spread:hardSet("Bias", 0.5)

  -- Gain defaults on wide-range adapters so 1 V CV sweeps the useful
  -- range. Identity (Gain = 1.0) is only meaningful for 0..1 params;
  -- the rest need explicit per-param gain.
  sweep:hardSet("Gain", 72.0)
  sweepTime:hardSet("Gain", 0.5)
  decay:hardSet("Gain", 2.0)
  hold:hardSet("Gain", 0.5)
  attack:hardSet("Gain", 0.05)

  tie(op, "Character", character, "Out")
  tie(op, "Shape",     shape,     "Out")
  tie(op, "Grit",      grit,      "Out")
  tie(op, "Punch",     punch,     "Out")
  tie(op, "Sweep",     sweep,     "Out")
  tie(op, "SweepTime", sweepTime, "Out")
  tie(op, "Attack",    attack,    "Out")
  tie(op, "Hold",      hold,      "Out")
  tie(op, "Decay",     decay,     "Out")
  tie(op, "Clipper",   clipper,   "Out")
  tie(op, "EQ",        eq,        "Out")
  tie(op, "Level",     level,     "Out")
  tie(op, "CompAmt",   compAmt,   "Out")
  tie(op, "Octave",    octave,    "Out")
  tie(op, "XformDepth",  depth,  "Out")
  tie(op, "XformSpread", spread, "Out")

  -- Register every top-level Bias the C++ op should mutate on randomize.
  -- Indices match the switch in applyRandomize. User asked to open
  -- everything to xform; downstream chain (Clipper/EQ/Level/Comp) and
  -- Sweep/SweepTime/Octave are now in the set.
  op:setTopLevelBias(0,  character:getParameter("Bias"))
  op:setTopLevelBias(1,  shape:getParameter("Bias"))
  op:setTopLevelBias(2,  grit:getParameter("Bias"))
  op:setTopLevelBias(3,  punch:getParameter("Bias"))
  op:setTopLevelBias(4,  attack:getParameter("Bias"))
  op:setTopLevelBias(5,  hold:getParameter("Bias"))
  op:setTopLevelBias(6,  decay:getParameter("Bias"))
  op:setTopLevelBias(7,  sweep:getParameter("Bias"))
  op:setTopLevelBias(8,  sweepTime:getParameter("Bias"))
  op:setTopLevelBias(9,  clipper:getParameter("Bias"))
  op:setTopLevelBias(10, eq:getParameter("Bias"))
  op:setTopLevelBias(11, level:getParameter("Bias"))
  op:setTopLevelBias(12, compAmt:getParameter("Bias"))
  op:setTopLevelBias(13, octave:getParameter("Bias"))

  local characterRange = self:addObject("characterRange", app.MinMax())
  local sweepRange     = self:addObject("sweepRange",     app.MinMax())
  local decayRange     = self:addObject("decayRange",     app.MinMax())
  local levelRange     = self:addObject("levelRange",     app.MinMax())
  connect(character, "Out", characterRange, "In")
  connect(sweep,     "Out", sweepRange,     "In")
  connect(decay,     "Out", decayRange,     "In")
  connect(level,     "Out", levelRange,     "In")

  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  self:addMonoBranch("trig",      trig,      "In", trig,      "Out")
  self:addMonoBranch("tune",      tune,      "In", tune,      "Out")
  self:addMonoBranch("character", character, "In", character, "Out")
  self:addMonoBranch("shape",     shape,     "In", shape,     "Out")
  self:addMonoBranch("grit",      grit,      "In", grit,      "Out")
  self:addMonoBranch("punch",     punch,     "In", punch,     "Out")
  self:addMonoBranch("sweep",     sweep,     "In", sweep,     "Out")
  self:addMonoBranch("sweepTime", sweepTime, "In", sweepTime, "Out")
  self:addMonoBranch("attack",    attack,    "In", attack,    "Out")
  self:addMonoBranch("hold",      hold,      "In", hold,      "Out")
  self:addMonoBranch("decay",     decay,     "In", decay,     "Out")
  self:addMonoBranch("clipper",   clipper,   "In", clipper,   "Out")
  self:addMonoBranch("eq",        eq,        "In", eq,        "Out")
  self:addMonoBranch("level",     level,     "In", level,     "Out")
  self:addMonoBranch("compAmt",   compAmt,   "In", compAmt,   "Out")
  self:addMonoBranch("octave",    octave,    "In", octave,    "Out")
  self:addMonoBranch("xformTrig", xformTrig, "In", xformTrig, "Out")
  self:addMonoBranch("depth",     depth,     "In", depth,     "Out")
  self:addMonoBranch("spread",    spread,    "In", spread,    "Out")
end

function DrumVoice:onLoadViews(objects, branches)
  local sweepMap = (function()
    local m = app.LinearDialMap(0, 72)
    m:setSteps(12, 1, 0.1, 0.01)
    return m
  end)()

  local decayMap = (function()
    local m = app.LinearDialMap(0.01, 2)
    m:setSteps(0.5, 0.1, 0.01, 0.001)
    return m
  end)()

  local sweepTimeMap = (function()
    local m = app.LinearDialMap(0.001, 0.5)
    m:setSteps(0.05, 0.01, 0.001, 0.0001)
    return m
  end)()

  local holdMap = (function()
    local m = app.LinearDialMap(0, 0.5)
    m:setSteps(0.05, 0.01, 0.001, 0.001)
    return m
  end)()

  local attackMap = (function()
    local m = app.LinearDialMap(0, 0.05)
    m:setSteps(0.005, 0.001, 0.001, 0.0001)
    return m
  end)()

  local octaveMap = (function()
    local m = app.LinearDialMap(-4, 4)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  local eqBipolarMap = (function()
    local m = app.LinearDialMap(-1, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  return {
    trig = Gate {
      button = "trig",
      description = "Trigger",
      branch = branches.trig,
      comparator = objects.trig
    },
    tune = DrumVoicePitchControl {
      button = "V/Oct",
      description = "V/Oct",
      branch = branches.tune,
      offset = objects.tune,
      range = objects.tuneRange,
      octaveParam = objects.octave:getParameter("Bias")
    },
    character = DrumVoiceCharacterControl {
      button = "char",
      description = "Character",
      branch = branches.character,
      gainbias = objects.character,
      range = objects.characterRange,
      biasMap = Encoder.getMap("[0,1]"),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5,
      op = objects.op,
      shapeParam = objects.shape:getParameter("Bias"),
      gritParam = objects.grit:getParameter("Bias"),
      punchParam = objects.punch:getParameter("Bias")
    },
    sweep = DrumVoiceSweepControl {
      button = "sweep",
      description = "Sweep",
      branch = branches.sweep,
      gainbias = objects.sweep,
      range = objects.sweepRange,
      biasMap = sweepMap,
      biasUnits = app.unitNone,
      biasPrecision = 1,
      initialBias = 18,
      sweepTimeParam = objects.sweepTime:getParameter("Bias")
    },
    decay = DrumVoiceDecayControl {
      button = "decay",
      description = "Decay",
      branch = branches.decay,
      gainbias = objects.decay,
      range = objects.decayRange,
      biasMap = decayMap,
      biasUnits = app.unitSecs,
      biasPrecision = 2,
      initialBias = 0.25,
      holdParam = objects.hold:getParameter("Bias"),
      attackParam = objects.attack:getParameter("Bias")
    },
    xform = DrumVoiceRandomGateControl {
      button = "xform",
      description = "Randomize",
      branch = branches.xformTrig,
      comparator = objects.xformTrig,
      op = objects.op,
      depthParam = objects.depth:getParameter("Bias"),
      spreadParam = objects.spread:getParameter("Bias")
    },
    level = DrumVoiceLevelControl {
      button = "level",
      description = "Level",
      branch = branches.level,
      gainbias = objects.level,
      range = objects.levelRange,
      biasMap = Encoder.getMap("[0,1]"),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.8,
      clipperParam = objects.clipper:getParameter("Bias"),
      eqParam = objects.eq:getParameter("Bias"),
      compParam = objects.compAmt:getParameter("Bias")
    },

    -- Auxiliary controls for expanded view: expose sub-display params at
    -- full width. Same ParameterAdapter as the headline control's shift
    -- sub-display, so edits on either surface converge on the same Bias.
    charShape = GainBias {
      button = "shape", description = "Shape",
      branch = branches.shape, gainbias = objects.shape, range = objects.shape,
      biasMap = Encoder.getMap("[0,1]"), biasUnits = app.unitNone,
      biasPrecision = 2, initialBias = 0.0
    },
    charGrit = GainBias {
      button = "grit", description = "Grit",
      branch = branches.grit, gainbias = objects.grit, range = objects.grit,
      biasMap = Encoder.getMap("[0,1]"), biasUnits = app.unitNone,
      biasPrecision = 2, initialBias = 0.0
    },
    charPunch = GainBias {
      button = "punch", description = "Punch",
      branch = branches.punch, gainbias = objects.punch, range = objects.punch,
      biasMap = Encoder.getMap("[0,1]"), biasUnits = app.unitNone,
      biasPrecision = 2, initialBias = 0.4
    },
    sweepTime = GainBias {
      button = "time", description = "Sweep Time",
      branch = branches.sweepTime, gainbias = objects.sweepTime, range = objects.sweepTime,
      biasMap = sweepTimeMap, biasUnits = app.unitSecs,
      biasPrecision = 3, initialBias = 0.04
    },
    decayHold = GainBias {
      button = "hold", description = "Hold",
      branch = branches.hold, gainbias = objects.hold, range = objects.hold,
      biasMap = holdMap, biasUnits = app.unitSecs,
      biasPrecision = 3, initialBias = 0.0
    },
    decayAttack = GainBias {
      button = "atk", description = "Attack",
      branch = branches.attack, gainbias = objects.attack, range = objects.attack,
      biasMap = attackMap, biasUnits = app.unitSecs,
      biasPrecision = 3, initialBias = 0.0
    },
    levelClipper = GainBias {
      button = "clip", description = "Clipper",
      branch = branches.clipper, gainbias = objects.clipper, range = objects.clipper,
      biasMap = Encoder.getMap("[0,1]"), biasUnits = app.unitNone,
      biasPrecision = 2, initialBias = 0.0
    },
    levelEQ = GainBias {
      button = "eq", description = "EQ",
      branch = branches.eq, gainbias = objects.eq, range = objects.eq,
      biasMap = eqBipolarMap, biasUnits = app.unitNone,
      biasPrecision = 2, initialBias = 0.0
    },
    levelComp = GainBias {
      button = "comp", description = "Compressor",
      branch = branches.compAmt, gainbias = objects.compAmt, range = objects.compAmt,
      biasMap = Encoder.getMap("[0,1]"), biasUnits = app.unitNone,
      biasPrecision = 2, initialBias = 0.0
    },
    tuneOctave = GainBias {
      button = "oct", description = "Octave",
      branch = branches.octave, gainbias = objects.octave, range = objects.octave,
      biasMap = octaveMap, biasUnits = app.unitNone,
      biasPrecision = 0, initialBias = 0
    },
    xformDepth = GainBias {
      button = "dpth", description = "Depth",
      branch = branches.depth, gainbias = objects.depth, range = objects.depth,
      biasMap = Encoder.getMap("[0,1]"), biasUnits = app.unitNone,
      biasPrecision = 2, initialBias = 0.3
    },
    xformSpread = GainBias {
      button = "sprd", description = "Spread",
      branch = branches.spread, gainbias = objects.spread, range = objects.spread,
      biasMap = Encoder.getMap("[0,1]"), biasUnits = app.unitNone,
      biasPrecision = 2, initialBias = 0.5
    }
  }, {
    expanded  = { "trig", "tune", "character", "sweep", "decay", "xform", "level" },
    tune      = { "tune", "tuneOctave" },
    character = { "character", "charShape", "charGrit", "charPunch" },
    sweep     = { "sweep", "sweepTime" },
    decay     = { "decay", "decayHold", "decayAttack" },
    xform     = { "xform", "xformDepth", "xformSpread" },
    level     = { "level", "levelClipper", "levelEQ", "levelComp" },
    collapsed = {}
  }
end

local adapterBiases = {
  "character", "shape", "grit", "punch", "sweep", "sweepTime",
  "attack", "hold", "decay", "clipper", "eq", "level", "compAmt", "octave",
  "depth", "spread"
}

function DrumVoice:serialize()
  local t = Unit.serialize(self)
  t.schema = 3 -- schema 3 = Makeup replaced with one-knob CompAmt
  for _, name in ipairs(adapterBiases) do
    local obj = self.objects[name]
    if obj then
      t[name] = obj:getParameter("Bias"):target()
    end
  end
  if self.objects.trig then
    t.trigThreshold = self.objects.trig:getParameter("Threshold"):target()
  end
  if self.objects.tune then
    t.tuneOffset = self.objects.tune:getParameter("Offset"):target()
  end
  return t
end

function DrumVoice:deserialize(t)
  -- Migration from schema 1 (pre-bipolar EQ): EQ was 0..1 with 0.5 bypass.
  -- Remap to -1..1 with 0 bypass. Detect by absence of schema tag and a
  -- value in the legacy range.
  if t.schema == nil and t.eq ~= nil and t.eq >= 0.0 and t.eq <= 1.0 then
    t.eq = (t.eq - 0.5) * 2.0
  end
  -- Migration from schema 1 / 2 (pre-comp): Makeup is gone; legacy t.makeup
  -- is silently dropped, compAmt defaults to 0 via the hardSet at init.
  -- Saved CV bindings against the makeup branch will fail to resolve and
  -- produce a log warning; user can re-bind to the compAmt branch.

  Unit.deserialize(self, t)
  for _, name in ipairs(adapterBiases) do
    if t[name] ~= nil and self.objects[name] then
      self.objects[name]:hardSet("Bias", t[name])
    end
  end
  if t.trigThreshold ~= nil and self.objects.trig then
    self.objects.trig:hardSet("Threshold", t.trigThreshold)
  end
  if t.tuneOffset ~= nil and self.objects.tune then
    self.objects.tune:hardSet("Offset", t.tuneOffset)
  end
end

return DrumVoice
