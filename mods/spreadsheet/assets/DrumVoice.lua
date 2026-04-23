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
  local octave     = self:addObject("octave",     app.ParameterAdapter())

  character:hardSet("Bias", 0.5)
  shape:hardSet("Bias", 0.0)
  grit:hardSet("Bias", 0.0)
  punch:hardSet("Bias", 0.3)
  sweep:hardSet("Bias", 12.0)
  sweepTime:hardSet("Bias", 0.03)
  attack:hardSet("Bias", 0.0)
  hold:hardSet("Bias", 0.0)
  decay:hardSet("Bias", 0.2)
  clipper:hardSet("Bias", 0.0)
  eq:hardSet("Bias", 0.5)
  level:hardSet("Bias", 0.8)
  octave:hardSet("Bias", 0.0)

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
  tie(op, "Octave",    octave,    "Out")

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
end

function DrumVoice:onLoadViews(objects, branches)
  local sweepMap = (function()
    local m = app.LinearDialMap(0, 72)
    m:setSteps(12, 1, 0.1, 0.01)
    return m
  end)()

  local decayMap = (function()
    local m = app.LinearDialMap(0.01, 5)
    m:setSteps(0.5, 0.1, 0.01, 0.001)
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
      initialBias = 12,
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
      initialBias = 0.2,
      holdParam = objects.hold:getParameter("Bias"),
      attackParam = objects.attack:getParameter("Bias")
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
      eqParam = objects.eq:getParameter("Bias")
    }
  }, {
    expanded = { "trig", "tune", "character", "sweep", "decay", "level" },
    collapsed = {}
  }
end

local adapterBiases = {
  "character", "shape", "grit", "punch", "sweep", "sweepTime",
  "attack", "hold", "decay", "clipper", "eq", "level", "octave"
}

function DrumVoice:serialize()
  local t = Unit.serialize(self)
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
