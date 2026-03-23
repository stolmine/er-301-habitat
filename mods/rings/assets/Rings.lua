local app = app
local librings = require "rings.librings"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Pitch = require "Unit.ViewControl.Pitch"
local EngineSelector = require "plaits.EngineSelector"
local MenuHeader = require "Unit.MenuControl.Header"
local Task = require "Unit.MenuControl.Task"
local Encoder = require "Encoder"

local modelMap = app.LinearDialMap(0, 5)
modelMap:setSteps(1, 1, 1, 1)
modelMap:setRounding(1)

local modelNames = {
  [0] = "Modal",
  [1] = "SympSt",
  [2] = "String",
  [3] = "FM",
  [4] = "SympQ",
  [5] = "Str+Rv"
}

local freqMap = app.LinearDialMap(-48, 48)
freqMap:setSteps(12, 1, 0.1, 0.01)

local Rings = Class {}
Rings:include(Unit)

function Rings:init(args)
  args.title = "Rings"
  args.mnemonic = "Rn"
  Unit.init(self, args)
end

function Rings:onLoadGraph(channelCount)
  local voice = self:addObject("voice", librings.RingsVoice())

  -- Exciter audio from chain input
  connect(self, "In1", voice, "In")

  -- V/Oct
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", voice, "V/Oct")

  -- Strum gate
  local strum = self:addObject("strum", app.Comparator())
  strum:setGateMode()
  connect(strum, "Out", voice, "Strum")

  -- Output
  connect(voice, "Out", self, "Out1")
  if channelCount > 1 then
    connect(voice, "Aux", self, "Out2")
  end

  -- Model
  local model = self:addObject("model", app.ParameterAdapter())
  model:hardSet("Bias", 0)
  tie(voice, "Model", model, "Out")
  self:addMonoBranch("model", model, "In", model, "Out")

  -- Freq (semitone offset)
  local freq = self:addObject("freq", app.ParameterAdapter())
  freq:hardSet("Bias", 0)
  tie(voice, "Freq", freq, "Out")
  self:addMonoBranch("freq", freq, "In", freq, "Out")

  -- Structure
  local structure = self:addObject("structure", app.ParameterAdapter())
  structure:hardSet("Bias", 0.5)
  tie(voice, "Structure", structure, "Out")
  self:addMonoBranch("structure", structure, "In", structure, "Out")

  -- Brightness
  local brightness = self:addObject("brightness", app.ParameterAdapter())
  brightness:hardSet("Bias", 0.5)
  tie(voice, "Brightness", brightness, "Out")
  self:addMonoBranch("brightness", brightness, "In", brightness, "Out")

  -- Damping
  local damping = self:addObject("damping", app.ParameterAdapter())
  damping:hardSet("Bias", 0.5)
  tie(voice, "Damping", damping, "Out")
  self:addMonoBranch("damping", damping, "In", damping, "Out")

  -- Position
  local position = self:addObject("position", app.ParameterAdapter())
  position:hardSet("Bias", 0.5)
  tie(voice, "Position", position, "Out")
  self:addMonoBranch("position", position, "In", position, "Out")

  -- Branches
  self:addMonoBranch("strum", strum, "In", strum, "Out")
  self:addMonoBranch("tune", tune, "In", tune, "Out")
end

-- Config menu
local menu = {
  "polyHeader",
  "poly1", "poly2", "poly4",
  "exciterHeader",
  "exciterToggle",
  "easterEggHeader",
  "easterEgg"
}

function Rings:onShowMenu(objects, branches)
  local controls = {}

  local poly = objects.voice:getOption("Polyphony"):value()
  controls.polyHeader = MenuHeader {
    description = string.format("Polyphony: %d", poly == 0 and 1 or poly == 1 and 2 or 4)
  }
  controls.poly1 = Task {
    description = "1 voice",
    task = function() objects.voice:setOptionValue("Polyphony", 0) end
  }
  controls.poly2 = Task {
    description = "2 voices",
    task = function() objects.voice:setOptionValue("Polyphony", 1) end
  }
  controls.poly4 = Task {
    description = "4 voices",
    task = function() objects.voice:setOptionValue("Polyphony", 2) end
  }

  local intExc = objects.voice:getOption("Int Exciter"):value() == 1
  controls.exciterHeader = MenuHeader {
    description = intExc and "Exciter: internal" or "Exciter: external"
  }
  controls.exciterToggle = Task {
    description = intExc and "use external" or "use internal",
    task = function()
      objects.voice:setOptionValue("Int Exciter", intExc and 0 or 1)
    end
  }

  local isEE = objects.voice:getOption("Easter Egg"):value() == 1
  controls.easterEggHeader = MenuHeader {
    description = isEE and "String Synth: ON" or "String Synth: OFF"
  }
  controls.easterEgg = Task {
    description = isEE and "disable" or "enable",
    task = function()
      objects.voice:setOptionValue("Easter Egg", isEE and 0 or 1)
    end
  }

  return controls, menu
end

local views = {
  expanded = {
    "strum",
    "tune",
    "model",
    "struct",
    "bright",
    "damp",
    "pos"
  },
  collapsed = {}
}

function Rings:onLoadViews(objects, branches)
  local controls = {}

  controls.strum = Gate {
    button = "strum",
    description = "Strum",
    branch = branches.strum,
    comparator = objects.strum
  }

  controls.tune = Pitch {
    button = "V/oct",
    branch = branches.tune,
    description = "V/oct",
    offset = objects.tune,
    range = objects.tuneRange
  }

  controls.model = EngineSelector {
    button = modelNames[0],
    description = "Model",
    branch = branches.model,
    gainbias = objects.model,
    range = objects.model,
    biasMap = modelMap,
    biasPrecision = 0,
    initialBias = 0,
    engineNames = modelNames
  }

  controls.struct = GainBias {
    button = "struct",
    description = "Structure",
    branch = branches.structure,
    gainbias = objects.structure,
    range = objects.structure,
    biasMap = Encoder.getMap("[0,1]"),
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.bright = GainBias {
    button = "bright",
    description = "Brightness",
    branch = branches.brightness,
    gainbias = objects.brightness,
    range = objects.brightness,
    biasMap = Encoder.getMap("[0,1]"),
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.damp = GainBias {
    button = "damp",
    description = "Damping",
    branch = branches.damping,
    gainbias = objects.damping,
    range = objects.damping,
    biasMap = Encoder.getMap("[0,1]"),
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.pos = GainBias {
    button = "pos",
    description = "Position",
    branch = branches.position,
    gainbias = objects.position,
    range = objects.position,
    biasMap = Encoder.getMap("[0,1]"),
    biasPrecision = 2,
    initialBias = 0.5
  }

  return controls, views
end

return Rings
