local app = app
local libplaits = require "plaits.libplaits"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local EngineSelector = require "plaits.EngineSelector"
local Gate = require "Unit.ViewControl.Gate"
local Pitch = require "Unit.ViewControl.Pitch"
local OptionControl = require "Unit.MenuControl.OptionControl"
local MenuHeader = require "Unit.MenuControl.Header"
local Task = require "Unit.MenuControl.Task"
local Encoder = require "Encoder"

local engineMap = app.LinearDialMap(0, 23)
engineMap:setSteps(1, 1, 1, 1)
engineMap:setRounding(1)

local freqMap = app.LinearDialMap(-48, 48)
freqMap:setSteps(1, 1, 0.1, 0.01)

-- Fader 0-23 maps to: original 16 first, then v1.2 additions
-- C++ applies the same remap to the actual Plaits engine index
local engineNames = {
  [0]  = "VA",
  [1]  = "WvShp",
  [2]  = "2opFM",
  [3]  = "Formt",
  [4]  = "Harm",
  [5]  = "WvTbl",
  [6]  = "Chord",
  [7]  = "Speech",
  [8]  = "Swarm",
  [9]  = "Noise",
  [10] = "Partcl",
  [11] = "String",
  [12] = "Modal",
  [13] = "Kick",
  [14] = "Snare",
  [15] = "HiHat",
  [16] = "VA+Flt",
  [17] = "PhsDst",
  [18] = "6opFM1",
  [19] = "6opFM2",
  [20] = "6opFM3",
  [21] = "WvTrrn",
  [22] = "StrMch",
  [23] = "Chip"
}

local Plaits = Class {}
Plaits:include(Unit)

function Plaits:init(args)
  args.title = "Plaits"
  args.mnemonic = "Pl"
  Unit.init(self, args)
end

function Plaits:onLoadGraph(channelCount)
  local voice = self:addObject("voice", libplaits.PlaitsVoice())

  -- V/Oct input (scaled 10x to match FULLSCALE_IN_VOLTS)
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  local voctGain = self:addObject("voctGain", app.ConstantGain())
  voctGain:hardSet("Gain", 10.0)
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", voctGain, "In")
  connect(voctGain, "Out", voice, "V/Oct")

  -- Trigger input
  local trig = self:addObject("trig", app.Comparator())
  trig:setGateMode()
  connect(trig, "Out", voice, "Trigger")

  -- Output routing
  if channelCount > 1 then
    connect(voice, "Out", self, "Out1")
    connect(voice, "Aux", self, "Out2")
  else
    connect(voice, "Out", self, "Out1")
  end

  -- Engine
  local engine = self:addObject("engine", app.ParameterAdapter())
  engine:hardSet("Bias", 0)
  tie(voice, "Engine", engine, "Out")
  self:addMonoBranch("engine", engine, "In", engine, "Out")

  -- Harmonics
  local harmonics = self:addObject("harmonics", app.ParameterAdapter())
  harmonics:hardSet("Bias", 0.5)
  tie(voice, "Harmonics", harmonics, "Out")
  self:addMonoBranch("harmonics", harmonics, "In", harmonics, "Out")

  -- Timbre
  local timbre = self:addObject("timbre", app.ParameterAdapter())
  timbre:hardSet("Bias", 0.5)
  tie(voice, "Timbre", timbre, "Out")
  self:addMonoBranch("timbre", timbre, "In", timbre, "Out")

  -- Morph
  local morph = self:addObject("morph", app.ParameterAdapter())
  morph:hardSet("Bias", 0.5)
  tie(voice, "Morph", morph, "Out")
  self:addMonoBranch("morph", morph, "In", morph, "Out")

  -- Freq (semitone offset from C4)
  local freq = self:addObject("freq", app.ParameterAdapter())
  freq:hardSet("Bias", 0.0)
  tie(voice, "Freq", freq, "Out")
  self:addMonoBranch("freq", freq, "In", freq, "Out")

  -- FM Amount
  local fmAmt = self:addObject("fmAmt", app.ParameterAdapter())
  fmAmt:hardSet("Bias", 0.0)
  tie(voice, "FM Amount", fmAmt, "Out")
  self:addMonoBranch("fmAmt", fmAmt, "In", fmAmt, "Out")

  -- Timbre CV Amount
  local timbreAmt = self:addObject("timbreAmt", app.ParameterAdapter())
  timbreAmt:hardSet("Bias", 0.0)
  tie(voice, "Timbre CV", timbreAmt, "Out")
  self:addMonoBranch("timbreAmt", timbreAmt, "In", timbreAmt, "Out")

  -- Morph CV Amount
  local morphAmt = self:addObject("morphAmt", app.ParameterAdapter())
  morphAmt:hardSet("Bias", 0.0)
  tie(voice, "Morph CV", morphAmt, "Out")
  self:addMonoBranch("morphAmt", morphAmt, "In", morphAmt, "Out")

  -- Decay
  local decay = self:addObject("decay", app.ParameterAdapter())
  decay:hardSet("Bias", 0.5)
  tie(voice, "Decay", decay, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")

  -- LPG Colour
  local lpgColour = self:addObject("lpgColour", app.ParameterAdapter())
  lpgColour:hardSet("Bias", 0.5)
  tie(voice, "LPG Colour", lpgColour, "Out")
  self:addMonoBranch("lpgColour", lpgColour, "In", lpgColour, "Out")

  -- Level CV input
  connect(self, "In1", voice, "Level")

  -- CV mod branches
  self:addMonoBranch("fm", voice, "FM", voice, "FM")
  self:addMonoBranch("timbreMod", voice, "Timbre Mod", voice, "Timbre Mod")
  self:addMonoBranch("morphMod", voice, "Morph Mod", voice, "Morph Mod")
  self:addMonoBranch("harmonicsMod", voice, "Harmonics Mod", voice, "Harmonics Mod")

  -- Trigger/Gate and tune branches
  self:addMonoBranch("trig", trig, "In", trig, "Out")
  self:addMonoBranch("tune", tune, "In", tune, "Out")
end

-- Views
local views = {
  expanded = {
    "tune",
    "freq",
    "engine",
    "harmonics",
    "timbre",
    "morph"
  },
  osc = {
    "tune",
    "freq",
    "engine",
    "harmonics",
    "timbre",
    "morph"
  },
  trig = {
    "gate",
    "tune",
    "freq",
    "engine",
    "harmonics",
    "timbre",
    "morph",
    "decay",
    "lpg"
  },
  collapsed = {}
}

function Plaits:changeMode(mode)
  if mode == "osc" then
    self.objects.voice:setOptionValue("Trig Mode", 1)
    self:switchView("osc")
  else
    self.objects.voice:setOptionValue("Trig Mode", 0)
    self:switchView("trig")
  end
end

-- Menu (header hold)
local menu = {
  "modeHeader",
  "setOscMode",
  "setTrigMode",
  "outputHeader",
  "output"
}

function Plaits:onShowMenu(objects, branches)
  local controls = {}

  controls.modeHeader = MenuHeader {
    description = "Mode:"
  }

  controls.setOscMode = Task {
    description = "osc (free-running)",
    task = function() self:changeMode("osc") end
  }

  controls.setTrigMode = Task {
    description = "trig (enveloped)",
    task = function() self:changeMode("trig") end
  }

  controls.outputHeader = MenuHeader {
    description = "Output:"
  }

  if self.channelCount == 1 then
    controls.output = OptionControl {
      description = "Output",
      option = objects.voice:getOption("Output Mode"),
      choices = { "main", "aux" }
    }
  else
    controls.output = OptionControl {
      description = "Output",
      option = objects.voice:getOption("Output Mode"),
      choices = { "main+aux", "aux+aux", "main+main", "aux+main" }
    }
  end

  return controls, menu
end

function Plaits:onLoadViews(objects, branches)
  local controls = {}

  controls.gate = Gate {
    button = "gate",
    description = "Gate",
    branch = branches.trig,
    comparator = objects.trig
  }

  controls.tune = Pitch {
    button = "V/oct",
    branch = branches.tune,
    description = "V/oct",
    offset = objects.tune,
    range = objects.tuneRange
  }

  controls.freq = GainBias {
    button = "freq",
    description = "Fundamental",
    branch = branches.freq,
    gainbias = objects.freq,
    range = objects.freq,
    biasMap = freqMap,
    biasPrecision = 1,
    initialBias = 0
  }

  controls.engine = EngineSelector {
    button = engineNames[0],
    description = "Engine",
    branch = branches.engine,
    gainbias = objects.engine,
    range = objects.engine,
    biasMap = engineMap,
    biasPrecision = 0,
    initialBias = 0,
    engineNames = engineNames
  }

  controls.harmonics = GainBias {
    button = "harm",
    description = "Harmonics",
    branch = branches.harmonics,
    gainbias = objects.harmonics,
    range = objects.harmonics,
    biasMap = Encoder.getMap("[0,1]"),
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.timbre = GainBias {
    button = "timb",
    description = "Timbre",
    branch = branches.timbre,
    gainbias = objects.timbre,
    range = objects.timbre,
    biasMap = Encoder.getMap("[0,1]"),
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.morph = GainBias {
    button = "morph",
    description = "Morph",
    branch = branches.morph,
    gainbias = objects.morph,
    range = objects.morph,
    biasMap = Encoder.getMap("[0,1]"),
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.decay = GainBias {
    button = "decay",
    description = "Decay",
    branch = branches.decay,
    gainbias = objects.decay,
    range = objects.decay,
    biasMap = Encoder.getMap("[0,1]"),
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.lpg = GainBias {
    button = "lpg",
    description = "LPG Colour",
    branch = branches.lpgColour,
    gainbias = objects.lpgColour,
    range = objects.lpgColour,
    biasMap = Encoder.getMap("[0,1]"),
    biasPrecision = 2,
    initialBias = 0.5
  }

  -- Default to osc view (matches default option value)
  local mode = objects.voice:getOption("Trig Mode"):value()
  if mode == 0 then
    return controls, views, "trig"
  else
    return controls, views, "osc"
  end
end

return Plaits
