local app = app
local libcatchall = require "catchall.libcatchall"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local SomScanControl = require "catchall.SomScanControl"
local SomModControl = require "catchall.SomModControl"

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local scanMap = floatMap(0, 1)
local plasticityMap = floatMap(0, 1)
local mixMap = floatMap(0, 1)
local parallaxMap = floatMap(-1, 1)
local modRateMap = (function()
  local m = app.LinearDialMap(0.001, 20)
  m:setSteps(1, 0.1, 0.01, 0.001)
  return m
end)()
local modShapeMap = floatMap(0, 1)
local modFbMap = floatMap(0, 0.95)
local outputLevelMap = floatMap(0, 2)
local nbrMap = (function()
  local m = app.LinearDialMap(0.05, 0.5)
  m:setSteps(0.05, 0.01, 0.001, 0.001)
  return m
end)()
local rateMap = (function()
  local m = app.LinearDialMap(0.01, 1)
  m:setSteps(0.05, 0.01, 0.001, 0.001)
  return m
end)()

local Som = Class {}
Som:include(Unit)

function Som:init(args)
  args.title = "Som"
  args.mnemonic = "Sm"
  Unit.init(self, args)
end

function Som:onLoadGraph(channelCount)
  local op = self:addObject("op", libcatchall.Som())
  op:setChannelCount(channelCount)

  connect(self, "In1", op, "In")
  connect(op, "Out1", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out2", self, "Out2")
  end

  local scanPos = self:addObject("scanPos", app.ParameterAdapter())
  scanPos:hardSet("Bias", 0.0)
  tie(op, "ScanPos", scanPos, "Out")
  self:addMonoBranch("scanPos", scanPos, "In", scanPos, "Out")

  local plasticity = self:addObject("plasticity", app.ParameterAdapter())
  plasticity:hardSet("Bias", 0.0)
  tie(op, "Plasticity", plasticity, "Out")
  self:addMonoBranch("plasticity", plasticity, "In", plasticity, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 0.5)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")

  local outputLevel = self:addObject("outputLevel", app.ParameterAdapter())
  outputLevel:hardSet("Bias", 1.0)
  tie(op, "OutputLevel", outputLevel, "Out")
  self:addMonoBranch("outputLevel", outputLevel, "In", outputLevel, "Out")

  local parallax = self:addObject("parallax", app.ParameterAdapter())
  parallax:hardSet("Bias", 0.0)
  tie(op, "Parallax", parallax, "Out")
  self:addMonoBranch("parallax", parallax, "In", parallax, "Out")

  local modAmount = self:addObject("modAmount", app.ParameterAdapter())
  modAmount:hardSet("Bias", 0.0)
  tie(op, "ModAmount", modAmount, "Out")
  self:addMonoBranch("modAmount", modAmount, "In", modAmount, "Out")

  local modRate = self:addObject("modRate", app.ParameterAdapter())
  modRate:hardSet("Bias", 0.1)
  tie(op, "ModRate", modRate, "Out")
  self:addMonoBranch("modRate", modRate, "In", modRate, "Out")

  local modShape = self:addObject("modShape", app.ParameterAdapter())
  modShape:hardSet("Bias", 0.0)
  tie(op, "ModShape", modShape, "Out")
  self:addMonoBranch("modShape", modShape, "In", modShape, "Out")

  local modFeedback = self:addObject("modFeedback", app.ParameterAdapter())
  modFeedback:hardSet("Bias", 0.0)
  tie(op, "ModFeedback", modFeedback, "Out")
  self:addMonoBranch("modFeedback", modFeedback, "In", modFeedback, "Out")

  local neighborhoodRadius = self:addObject("neighborhoodRadius", app.ParameterAdapter())
  neighborhoodRadius:hardSet("Bias", 0.06)
  tie(op, "NeighborhoodRadius", neighborhoodRadius, "Out")
  self:addMonoBranch("neighborhoodRadius", neighborhoodRadius, "In", neighborhoodRadius, "Out")

  local learningRate = self:addObject("learningRate", app.ParameterAdapter())
  learningRate:hardSet("Bias", 0.1)
  tie(op, "LearningRate", learningRate, "Out")
  self:addMonoBranch("learningRate", learningRate, "In", learningRate, "Out")

  local feedback = self:addObject("feedback", app.ParameterAdapter())
  feedback:hardSet("Bias", 0.0)
  tie(op, "Feedback", feedback, "Out")
  self:addMonoBranch("feedback", feedback, "In", feedback, "Out")

  local decay = self:addObject("decay", app.ParameterAdapter())
  decay:hardSet("Bias", 0.995)
  tie(op, "Decay", decay, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")
end

function Som:onLoadViews(objects, branches)
  local controls = {}

  controls.scan = SomScanControl {
    button = "scan",
    description = "Scan",
    branch = branches.scanPos,
    gainbias = objects.scanPos,
    range = objects.scanPos,
    biasMap = scanMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0,
    op = objects.op,
    nbrParam = objects.neighborhoodRadius:getParameter("Bias"),
    rateParam = objects.learningRate:getParameter("Bias"),
    decayParam = objects.decay:getParameter("Bias")
  }

  controls.plasticity = GainBias {
    button = "plst",
    description = "Plasticity",
    branch = branches.plasticity,
    gainbias = objects.plasticity,
    range = objects.plasticity,
    biasMap = plasticityMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.mix = GainBias {
    button = "mix",
    description = "Mix",
    branch = branches.mix,
    gainbias = objects.mix,
    range = objects.mix,
    biasMap = mixMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.neighborhood = GainBias {
    button = "nbr",
    description = "Neighborhood Radius",
    branch = branches.neighborhoodRadius,
    gainbias = objects.neighborhoodRadius,
    range = objects.neighborhoodRadius,
    biasMap = nbrMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.06
  }

  controls.rate = GainBias {
    button = "rate",
    description = "Learning Rate",
    branch = branches.learningRate,
    gainbias = objects.learningRate,
    range = objects.learningRate,
    biasMap = rateMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.1
  }

  controls.parallax = GainBias {
    button = "prlx",
    description = "Parallax",
    branch = branches.parallax,
    gainbias = objects.parallax,
    range = objects.parallax,
    biasMap = parallaxMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.mod = SomModControl {
    button = "mod",
    description = "Mod Amount",
    branch = branches.modAmount,
    gainbias = objects.modAmount,
    range = objects.modAmount,
    biasMap = floatMap(0, 1),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0,
    rateParam = objects.modRate:getParameter("Bias"),
    shapeParam = objects.modShape:getParameter("Bias"),
    fbParam = objects.modFeedback:getParameter("Bias")
  }

  controls.modRate = GainBias {
    button = "rate",
    description = "Mod Rate",
    branch = branches.modRate,
    gainbias = objects.modRate,
    range = objects.modRate,
    biasMap = modRateMap,
    biasUnits = app.unitHertz,
    biasPrecision = 2,
    initialBias = 0.1
  }

  controls.modShape = GainBias {
    button = "shape",
    description = "Mod Shape",
    branch = branches.modShape,
    gainbias = objects.modShape,
    range = objects.modShape,
    biasMap = modShapeMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.modFeedback = GainBias {
    button = "fb",
    description = "Mod Feedback",
    branch = branches.modFeedback,
    gainbias = objects.modFeedback,
    range = objects.modFeedback,
    biasMap = modFbMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.feedback = GainBias {
    button = "fdbk",
    description = "Feedback",
    branch = branches.feedback,
    gainbias = objects.feedback,
    range = objects.feedback,
    biasMap = floatMap(0, 1),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.level = GainBias {
    button = "lvl",
    description = "Output Level",
    branch = branches.outputLevel,
    gainbias = objects.outputLevel,
    range = objects.outputLevel,
    biasMap = outputLevelMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }

  controls.decay = GainBias {
    button = "decay",
    description = "Decay",
    branch = branches.decay,
    gainbias = objects.decay,
    range = objects.decay,
    biasMap = (function()
      local m = app.LinearDialMap(0.9, 1.0)
      m:setSteps(0.01, 0.001, 0.0001, 0.0001)
      return m
    end)(),
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.995
  }

  local views = {
    expanded = { "scan", "plasticity", "parallax", "mod", "feedback", "mix" },
    collapsed = {},
    scan = { "scan", "neighborhood", "rate", "decay" },
    mod = { "mod", "modRate", "modShape", "modFeedback" }
  }

  return controls, views
end

local adapterBiases = {
  "scanPos", "plasticity", "parallax", "modAmount", "modRate", "modShape", "modFeedback",
  "mix", "outputLevel", "neighborhoodRadius", "learningRate", "feedback", "decay"
}

function Som:serialize()
  local t = Unit.serialize(self)
  for _, name in ipairs(adapterBiases) do
    local o = self.objects[name]
    if o then
      t[name] = o:getParameter("Bias"):target()
    end
  end
  return t
end

function Som:deserialize(t)
  Unit.deserialize(self, t)
  for _, name in ipairs(adapterBiases) do
    if t[name] ~= nil and self.objects[name] then
      self.objects[name]:hardSet("Bias", t[name])
    end
  end
end

return Som
