local app = app
local libstolmine = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Pitch = require "Unit.ViewControl.Pitch"
local MixControl = require "spreadsheet.MixControl"
local TransformGateControl = require "spreadsheet.TransformGateControl"
local DensityControl = require "biome.DensityControl"
local ModeSelector = require "biome.ModeSelector"
local Encoder = require "Encoder"

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(4, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local sizeMap = (function()
  local map = app.LinearDialMap(0.001, 2.0)
  map:setSteps(0.1, 0.01, 0.001, 0.0001)
  return map
end)()

local feedbackMap = floatMap(0, 0.99)
local mixMap = floatMap(0, 1)
local inputLevelMap = floatMap(0, 4)
local outputLevelMap = floatMap(0, 4)
local tanhMap = floatMap(0, 1)
local densityMap = intMap(1, 64)

local xformTargetNames = {
  [0] = "all", "size", "fdbk", "res", "dens", "patt", "slope", "mix", "reset"
}

local Pecto = Class {}
Pecto:include(Unit)

function Pecto:init(args)
  args.title = "Pecto"
  args.mnemonic = "Pc"
  Unit.init(self, args)
end

function Pecto:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.Pecto())
  op:allocateTimeUpTo(2.0)

  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    local opR = self:addObject("opR", libstolmine.Pecto())
    opR:allocateTimeUpTo(2.0)
    connect(self, "In2", opR, "In")
    connect(opR, "Out", self, "Out2")
  end

  local stereo = channelCount > 1

  local function tieParam(name, adapter)
    tie(op, name, adapter, "Out")
    if stereo then tie(self.objects.opR, name, adapter, "Out") end
  end

  -- V/Oct
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  local vOctAdapter = self:addObject("vOctAdapter", app.ParameterAdapter())
  vOctAdapter:hardSet("Gain", 1.0)
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", vOctAdapter, "In")
  tieParam("VOctPitch", vOctAdapter)
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  -- Comb size
  local combSize = self:addObject("combSize", app.ParameterAdapter())
  combSize:hardSet("Bias", 0.1)
  tieParam("CombSize", combSize)
  self:addMonoBranch("combSize", combSize, "In", combSize, "Out")

  -- Density
  local density = self:addObject("density", app.ParameterAdapter())
  density:hardSet("Bias", 8)
  tieParam("Density", density)
  self:addMonoBranch("density", density, "In", density, "Out")

  -- Pattern
  local pattern = self:addObject("pattern", app.ParameterAdapter())
  pattern:hardSet("Bias", 0)
  tieParam("Pattern", pattern)
  self:addMonoBranch("pattern", pattern, "In", pattern, "Out")

  -- Slope
  local slopeParam = self:addObject("slope", app.ParameterAdapter())
  slopeParam:hardSet("Bias", 0)
  tieParam("Slope", slopeParam)
  self:addMonoBranch("slope", slopeParam, "In", slopeParam, "Out")

  -- Resonator type
  local resonatorType = self:addObject("resonatorType", app.ParameterAdapter())
  resonatorType:hardSet("Bias", 0)
  tieParam("ResonatorType", resonatorType)
  self:addMonoBranch("resonatorType", resonatorType, "In", resonatorType, "Out")

  -- Feedback
  local feedback = self:addObject("feedback", app.ParameterAdapter())
  feedback:hardSet("Bias", 0.5)
  tieParam("Feedback", feedback)
  self:addMonoBranch("feedback", feedback, "In", feedback, "Out")

  -- Mix
  local mixParam = self:addObject("mix", app.ParameterAdapter())
  mixParam:hardSet("Bias", 0.5)
  tieParam("Mix", mixParam)
  self:addMonoBranch("mix", mixParam, "In", mixParam, "Out")

  -- Input/output/tanh
  local inputLevel = self:addObject("inputLevel", app.ParameterAdapter())
  inputLevel:hardSet("Bias", 1.0)
  tieParam("InputLevel", inputLevel)
  self:addMonoBranch("inputLevel", inputLevel, "In", inputLevel, "Out")

  local outputLevel = self:addObject("outputLevel", app.ParameterAdapter())
  outputLevel:hardSet("Bias", 1.0)
  tieParam("OutputLevel", outputLevel)
  self:addMonoBranch("outputLevel", outputLevel, "In", outputLevel, "Out")

  local tanhAmt = self:addObject("tanhAmt", app.ParameterAdapter())
  tanhAmt:hardSet("Bias", 0.0)
  tieParam("TanhAmt", tanhAmt)
  self:addMonoBranch("tanhAmt", tanhAmt, "In", tanhAmt, "Out")

  -- Xform gate
  local xformGate = self:addObject("xformGate", app.Comparator())
  xformGate:setTriggerMode()
  connect(xformGate, "Out", op, "XformGate")
  if stereo then connect(xformGate, "Out", self.objects.opR, "XformGate") end
  self:addMonoBranch("xformGate", xformGate, "In", xformGate, "Out")

  local xformTarget = self:addObject("xformTarget", app.ParameterAdapter())
  xformTarget:hardSet("Bias", 0)
  tieParam("XformTarget", xformTarget)
  self:addMonoBranch("xformTarget", xformTarget, "In", xformTarget, "Out")

  local xformDepth = self:addObject("xformDepth", app.ParameterAdapter())
  xformDepth:hardSet("Bias", 0.5)
  tieParam("XformDepth", xformDepth)
  self:addMonoBranch("xformDepth", xformDepth, "In", xformDepth, "Out")

  -- Top-level bias refs for randomization (shared, so only on op)
  op:setTopLevelBias(0, combSize:getParameter("Bias"))
  op:setTopLevelBias(1, feedback:getParameter("Bias"))
  op:setTopLevelBias(2, resonatorType:getParameter("Bias"))
  op:setTopLevelBias(3, density:getParameter("Bias"))
  op:setTopLevelBias(4, pattern:getParameter("Bias"))
  op:setTopLevelBias(5, slopeParam:getParameter("Bias"))
  op:setTopLevelBias(6, mixParam:getParameter("Bias"))
end

function Pecto:fireTransform()
  self.objects.op:fireRandomize()
end

function Pecto:onLoadViews()
  return {
    tune = Pitch {
      button = "V/Oct",
      branch = self.branches.tune,
      description = "V/Oct",
      offset = self.objects.tune,
      range = self.objects.tuneRange
    },
    size = GainBias {
      button = "size",
      description = "Comb Size",
      branch = self.branches.combSize,
      gainbias = self.objects.combSize,
      range = self.objects.combSize,
      biasMap = sizeMap,
      biasUnits = app.unitSecs,
      biasPrecision = 3,
      initialBias = 0.1
    },
    density = DensityControl {
      button = "dens",
      description = "Density",
      branch = self.branches.density,
      gainbias = self.objects.density,
      range = self.objects.density,
      biasMap = densityMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 8,
      pattern = self.objects.pattern:getParameter("Bias"),
      slope = self.objects.slope:getParameter("Bias"),
      resonator = self.objects.resonatorType:getParameter("Bias")
    },
    feedback = GainBias {
      button = "fdbk",
      description = "Feedback",
      branch = self.branches.feedback,
      gainbias = self.objects.feedback,
      range = self.objects.feedback,
      biasMap = feedbackMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    xform = TransformGateControl {
      seq = self,
      button = "xform",
      description = "Randomize",
      branch = self.branches.xformGate,
      comparator = self.objects.xformGate,
      funcNames = xformTargetNames,
      funcMap = intMap(0, 8),
      funcParam = self.objects.xformTarget:getParameter("Bias"),
      paramALabel = "depth",
      factorParam = self.objects.xformDepth:getParameter("Bias"),
      factorMap = floatMap(0, 1),
      factorPrecision = 2
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
      initialBias = 0.5,
      inputLevel = self.objects.inputLevel:getParameter("Bias"),
      outputLevel = self.objects.outputLevel:getParameter("Bias"),
      tanhAmt = self.objects.tanhAmt:getParameter("Bias")
    },
    -- Expansion controls
    densityFader = GainBias {
      button = "dens",
      description = "Density",
      branch = self.branches.density,
      gainbias = self.objects.density,
      range = self.objects.density,
      biasMap = densityMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 8
    },
    patternFader = ModeSelector {
      button = "patt",
      description = "Pattern",
      branch = self.branches.pattern,
      gainbias = self.objects.pattern,
      range = self.objects.pattern,
      biasMap = intMap(0, 15),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = {
        [0] = "unif", "fib", "early", "late", "mid", "ess", "flat", "rfib",
        [8] = "r.un", "r.fi", "r.ea", "r.la", "r.mi", "r.es", "r.fl", "r.rf"
      }
    },
    slopeFader = ModeSelector {
      button = "slope",
      description = "Slope",
      branch = self.branches.slope,
      gainbias = self.objects.slope,
      range = self.objects.slope,
      biasMap = intMap(0, 3),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = { [0] = "flat", "rise", "fall", "hump" }
    },
    resonatorFader = ModeSelector {
      button = "res",
      description = "Resonator",
      branch = self.branches.resonatorType,
      gainbias = self.objects.resonatorType,
      range = self.objects.resonatorType,
      biasMap = intMap(0, 3),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = { [0] = "raw", "gtr", "clar", "sitr" }
    },
    inputLevel = GainBias {
      button = "input",
      description = "Input Level",
      branch = self.branches.inputLevel,
      gainbias = self.objects.inputLevel,
      range = self.objects.inputLevel,
      biasMap = inputLevelMap,
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
      biasMap = outputLevelMap,
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
    expanded = { "tune", "size", "density", "feedback", "xform", "mix" },
    collapsed = {},
    density = { "densityFader", "patternFader", "slopeFader", "resonatorFader" },
    mix = { "mix", "inputLevel", "outputLevel", "tanhAmt" }
  }
end

return Pecto
