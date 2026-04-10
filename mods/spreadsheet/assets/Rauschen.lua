local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local ViewControl = require "Unit.ViewControl"
local ModeSelector = require "spreadsheet.ModeSelector"
local RauschenCutoffControl = require "spreadsheet.RauschenCutoffControl"
local ThresholdFader = require "spreadsheet.ThresholdFader"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY

local algoNames = {
  [0] = "White",
  [1] = "Pink",
  [2] = "Dust",
  [3] = "Particle",
  [4] = "Crackle",
  [5] = "Logistic",
  [6] = "Henon",
  [7] = "Clocked",
  [8] = "Velvet",
  [9] = "Gendy",
  [10] = "Lorenz"
}

local algoMap = (function()
  local m = app.LinearDialMap(0, 10)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local paramMap = floatMap(0, 1)
local levelMap = floatMap(0, 1)

local cutoffMap = (function()
  local m = app.LinearDialMap(20, 20000)
  m:setSteps(1000, 100, 10, 1)
  return m
end)()

local morphMap = floatMap(0, 1)
local qMap = (function()
  local m = app.LinearDialMap(0.5, 20)
  m:setSteps(1, 0.1, 0.01, 0.01)
  return m
end)()

local Rauschen = Class {}
Rauschen:include(Unit)

function Rauschen:init(args)
  args.title = "Rauschen"
  args.mnemonic = "Rn"
  Unit.init(self, args)
end

function Rauschen:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Rauschen())

  -- Sink chain input (generator)
  local sink = self:addObject("sink", app.ConstantGain())
  sink:hardSet("Gain", 0.0)
  connect(self, "In1", sink, "In")

  -- V/Oct
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")

  -- Output
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  -- Algorithm
  local algo = self:addObject("algo", app.ParameterAdapter())
  algo:hardSet("Bias", 0.0)
  tie(op, "Algorithm", algo, "Out")
  self:addMonoBranch("algo", algo, "In", algo, "Out")

  -- Param X
  local paramX = self:addObject("paramX", app.ParameterAdapter())
  paramX:hardSet("Bias", 0.5)
  tie(op, "ParamX", paramX, "Out")
  self:addMonoBranch("paramX", paramX, "In", paramX, "Out")

  -- Param Y
  local paramY = self:addObject("paramY", app.ParameterAdapter())
  paramY:hardSet("Bias", 0.5)
  tie(op, "ParamY", paramY, "Out")
  self:addMonoBranch("paramY", paramY, "In", paramY, "Out")

  -- Filter Freq
  local filterFreq = self:addObject("filterFreq", app.ParameterAdapter())
  filterFreq:hardSet("Bias", 10000.0)
  tie(op, "FilterFreq", filterFreq, "Out")
  self:addMonoBranch("filterFreq", filterFreq, "In", filterFreq, "Out")

  -- Filter Q
  local filterQ = self:addObject("filterQ", app.ParameterAdapter())
  filterQ:hardSet("Bias", 0.5)
  tie(op, "FilterQ", filterQ, "Out")
  self:addMonoBranch("filterQ", filterQ, "In", filterQ, "Out")

  -- Filter Morph
  local filterMorph = self:addObject("filterMorph", app.ParameterAdapter())
  filterMorph:hardSet("Bias", 0.1)
  tie(op, "FilterMorph", filterMorph, "Out")
  self:addMonoBranch("filterMorph", filterMorph, "In", filterMorph, "Out")

  -- Level
  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Bias", 0.5)
  tie(op, "Level", level, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")

  -- V/Oct branch
  self:addMonoBranch("tune", tune, "In", tune, "Out")
end

function Rauschen:onLoadViews(objects, branches)
  -- Phase space viz (read-only display, no control)
  local vizView = Class {}
  vizView:include(ViewControl)

  function vizView:init(args)
    ViewControl.init(self)
    self:setClassName("Rauschen.PhaseSpaceView")
    local graphic = app.Graphic(0, 0, ply, 64)
    self:setMainCursorController(graphic)
    self:setControlGraphic(graphic)
    self:addSpotDescriptor{center = ply * 0.5}

    local phaseSpace = libspreadsheet.PhaseSpaceGraphic(0, 0, ply, 64)
    phaseSpace:follow(args.dspObject)
    graphic:addChild(phaseSpace)
  end

  return {
    algo = ModeSelector {
      button = "algo",
      description = "Algorithm",
      branch = branches.algo,
      gainbias = objects.algo,
      range = objects.algo,
      biasMap = algoMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = algoNames
    },
    viz = vizView {
      dspObject = objects.op
    },
    paramX = GainBias {
      button = "X",
      description = "Param X",
      branch = branches.paramX,
      gainbias = objects.paramX,
      range = objects.paramX,
      biasMap = paramMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    paramY = GainBias {
      button = "Y",
      description = "Param Y",
      branch = branches.paramY,
      gainbias = objects.paramY,
      range = objects.paramY,
      biasMap = paramMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    cutoff = RauschenCutoffControl {
      button = "cutoff",
      description = "Filter Cutoff",
      branch = branches.filterFreq,
      gainbias = objects.filterFreq,
      range = objects.filterFreq,
      biasMap = cutoffMap,
      biasUnits = app.unitHertz,
      biasPrecision = 0,
      initialBias = 10000.0,
      filterMorph = objects.filterMorph:getParameter("Bias"),
      filterQ = objects.filterQ:getParameter("Bias")
    },
    level = GainBias {
      button = "level",
      description = "Level",
      branch = branches.level,
      gainbias = objects.level,
      range = objects.level,
      biasMap = levelMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    morph = ThresholdFader {
      button = "morph",
      description = "Filter Morph",
      branch = branches.filterMorph,
      gainbias = objects.filterMorph,
      range = objects.filterMorph,
      biasMap = morphMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.1,
      thresholdLabels = {
        {0.0, "off"}, {0.005, "LP"}, {0.08, "L>B"}, {0.17, "BP"},
        {0.33, "B>H"}, {0.42, "HP"}, {0.58, "H>N"}, {0.67, "ntch"}
      }
    },
    filterQ = GainBias {
      button = "Q",
      description = "Filter Q",
      branch = branches.filterQ,
      gainbias = objects.filterQ,
      range = objects.filterQ,
      biasMap = qMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    }
  }, {
    expanded = { "algo", "viz", "paramX", "paramY", "cutoff", "level" },
    collapsed = {},
    cutoff = { "cutoff", "morph", "filterQ" }
  }
end

return Rauschen
