local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local MixControl = require "spreadsheet.MixControl"
local LaretClockControl = require "spreadsheet.LaretClockControl"
local ColmatageBlockControl = require "spreadsheet.ColmatageBlockControl"
local ColmatageRepeatsControl = require "spreadsheet.ColmatageRepeatsControl"
local ColmatageTextureControl = require "spreadsheet.ColmatageTextureControl"
local Encoder = require "Encoder"

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(4, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local densityMap = floatMap(0, 1)
local blockSizeMap = floatMap(0, 1)
local blockMaxMap = intMap(1, 16)
local repeatCountMap = intMap(2, 64)
local ritardMap = floatMap(0, 1)
local blendMap = floatMap(0, 1)
local accelMap = floatMap(0.5, 0.999)
local dutyMap = floatMap(-1, 1)
local ampMap = floatMap(0, 1)
local fadeMap = floatMap(0, 0.1)
local mixMap = floatMap(0, 1)
local levelMap = floatMap(0, 4)
local tanhMap = floatMap(0, 1)
local phraseMap = intMap(1, 8)
local subdivMap = intMap(6, 32)

local Colmatage = Class {}
Colmatage:include(Unit)

function Colmatage:init(args)
  args.title = "Colmatage"
  args.mnemonic = "BC"
  Unit.init(self, args)
end

function Colmatage:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Colmatage())

  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  local clock = self:addObject("clock", app.Comparator())
  clock:setTriggerMode()
  connect(clock, "Out", op, "Clock")
  self:addMonoBranch("clock", clock, "In", clock, "Out")

  local reset = self:addObject("reset", app.Comparator())
  reset:setTriggerMode()
  connect(reset, "Out", op, "Reset")
  self:addMonoBranch("reset", reset, "In", reset, "Out")

  local function adapter(name, param, initial)
    local a = self:addObject(name, app.ParameterAdapter())
    a:hardSet("Bias", initial)
    tie(op, param, a, "Out")
    self:addMonoBranch(name, a, "In", a, "Out")
  end

  adapter("density",      "Density",      0.5)
  adapter("blockSize",    "BlockSize",    0.5)
  adapter("blockMax",     "BlockMax",     8)
  adapter("repeatCount",  "RepeatCount",  4)
  adapter("ritardBias",   "RitardBias",   0.5)
  adapter("blend",        "Blend",        0.5)
  adapter("accel",        "Accel",        0.9)
  adapter("subdiv",       "Subdiv",       8)
  adapter("phraseMin",    "PhraseMin",    2)
  adapter("phraseMax",    "PhraseMax",    4)
  adapter("dutyCycle",    "DutyCycle",    1.0)
  adapter("ampMin",       "AmpMin",       0.8)
  adapter("ampMax",       "AmpMax",       1.0)
  adapter("fade",         "Fade",         0.005)
  adapter("mix",          "Mix",          1.0)
  adapter("inputLevel",   "InputLevel",   1.0)
  adapter("outputLevel",  "OutputLevel",  1.0)
  adapter("tanhAmt",      "TanhAmt",      0.0)
end

function Colmatage:onLoadViews()
  return {
    clock = LaretClockControl {
      button = "clock",
      description = "Clock",
      branch = self.branches.clock,
      comparator = self.objects.clock,
      resetComparator = self.objects.reset,
      divParam = self.objects.subdiv:getParameter("Bias")
    },
    block = ColmatageBlockControl {
      button = "block",
      description = "Block Size",
      branch = self.branches.blockSize,
      gainbias = self.objects.blockSize,
      range = self.objects.blockSize,
      biasMap = blockSizeMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5,
      op = self.objects.op,
      phraseMin = self.objects.phraseMin:getParameter("Bias"),
      phraseMax = self.objects.phraseMax:getParameter("Bias"),
      blockMax = self.objects.blockMax:getParameter("Bias")
    },
    density = GainBias {
      button = "dens",
      description = "Density",
      branch = self.branches.density,
      gainbias = self.objects.density,
      range = self.objects.density,
      biasMap = densityMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    repeats = ColmatageRepeatsControl {
      button = "rep",
      description = "Repeats",
      branch = self.branches.repeatCount,
      gainbias = self.objects.repeatCount,
      range = self.objects.repeatCount,
      biasMap = repeatCountMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 8,
      ritardBias = self.objects.ritardBias:getParameter("Bias"),
      blend = self.objects.blend:getParameter("Bias"),
      accel = self.objects.accel:getParameter("Bias")
    },
    texture = ColmatageTextureControl {
      button = "duty",
      description = "Duty Cycle",
      branch = self.branches.dutyCycle,
      gainbias = self.objects.dutyCycle,
      range = self.objects.dutyCycle,
      biasMap = dutyMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0,
      ampMin = self.objects.ampMin:getParameter("Bias"),
      ampMax = self.objects.ampMax:getParameter("Bias"),
      fade = self.objects.fade:getParameter("Bias")
    },
    mix = MixControl {
      button = "mix",
      description = "Mix",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = mixMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0,
      inputLevel = self.objects.inputLevel:getParameter("Bias"),
      outputLevel = self.objects.outputLevel:getParameter("Bias"),
      tanhAmt = self.objects.tanhAmt:getParameter("Bias")
    },
    -- Expansion controls
    resetGate = Gate {
      button = "reset",
      description = "Reset",
      branch = self.branches.reset,
      comparator = self.objects.reset
    },
    subdivFader = GainBias {
      button = "subdv",
      description = "Subdivision",
      branch = self.branches.subdiv,
      gainbias = self.objects.subdiv,
      range = self.objects.subdiv,
      biasMap = subdivMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 8
    },
    phraseMin = GainBias {
      button = "pMin",
      description = "Phrase Min",
      branch = self.branches.phraseMin,
      gainbias = self.objects.phraseMin,
      range = self.objects.phraseMin,
      biasMap = phraseMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 2
    },
    phraseMax = GainBias {
      button = "pMax",
      description = "Phrase Max",
      branch = self.branches.phraseMax,
      gainbias = self.objects.phraseMax,
      range = self.objects.phraseMax,
      biasMap = phraseMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 4
    },
    blockMaxFader = GainBias {
      button = "bMax",
      description = "Block Max",
      branch = self.branches.blockMax,
      gainbias = self.objects.blockMax,
      range = self.objects.blockMax,
      biasMap = blockMaxMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 8
    },
    repeatsFader = GainBias {
      button = "rep",
      description = "Repeats",
      branch = self.branches.repeatCount,
      gainbias = self.objects.repeatCount,
      range = self.objects.repeatCount,
      biasMap = repeatCountMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 4
    },
    ritardFader = GainBias {
      button = "rit",
      description = "Ritard Bias",
      branch = self.branches.ritardBias,
      gainbias = self.objects.ritardBias,
      range = self.objects.ritardBias,
      biasMap = ritardMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    blendFader = GainBias {
      button = "blnd",
      description = "Blend",
      branch = self.branches.blend,
      gainbias = self.objects.blend,
      range = self.objects.blend,
      biasMap = blendMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    accelFader = GainBias {
      button = "accel",
      description = "Accel",
      branch = self.branches.accel,
      gainbias = self.objects.accel,
      range = self.objects.accel,
      biasMap = accelMap,
      biasUnits = app.unitNone,
      biasPrecision = 3,
      initialBias = 0.9
    },
    textureFader = GainBias {
      button = "duty",
      description = "Duty Cycle",
      branch = self.branches.dutyCycle,
      gainbias = self.objects.dutyCycle,
      range = self.objects.dutyCycle,
      biasMap = dutyMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    ampMinFader = GainBias {
      button = "amin",
      description = "Amp Min",
      branch = self.branches.ampMin,
      gainbias = self.objects.ampMin,
      range = self.objects.ampMin,
      biasMap = ampMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.8
    },
    ampMaxFader = GainBias {
      button = "amax",
      description = "Amp Max",
      branch = self.branches.ampMax,
      gainbias = self.objects.ampMax,
      range = self.objects.ampMax,
      biasMap = ampMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    fadeFader = GainBias {
      button = "fade",
      description = "Fade",
      branch = self.branches.fade,
      gainbias = self.objects.fade,
      range = self.objects.fade,
      biasMap = fadeMap,
      biasUnits = app.unitSecs,
      biasPrecision = 3,
      initialBias = 0.005
    },
    inputLevel = GainBias {
      button = "input",
      description = "Input Level",
      branch = self.branches.inputLevel,
      gainbias = self.objects.inputLevel,
      range = self.objects.inputLevel,
      biasMap = levelMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    outputLevel = GainBias {
      button = "out",
      description = "Output Level",
      branch = self.branches.outputLevel,
      gainbias = self.objects.outputLevel,
      range = self.objects.outputLevel,
      biasMap = levelMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    tanhAmt = GainBias {
      button = "tanh",
      description = "Saturation",
      branch = self.branches.tanhAmt,
      gainbias = self.objects.tanhAmt,
      range = self.objects.tanhAmt,
      biasMap = tanhMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    }
  }, {
    expanded  = { "clock", "block", "density", "repeats", "texture", "mix" },
    collapsed = {},
    clock     = { "clock", "resetGate", "subdivFader" },
    block     = { "block", "phraseMin", "phraseMax", "blockMaxFader" },
    repeats   = { "repeatsFader", "ritardFader", "blendFader", "accelFader" },
    texture   = { "textureFader", "ampMinFader", "ampMaxFader", "fadeFader" },
    mix       = { "mix", "inputLevel", "outputLevel", "tanhAmt" }
  }
end

return Colmatage
