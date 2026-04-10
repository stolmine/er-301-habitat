local app = app
local libcatchall = require "catchall.libcatchall"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local ViewControl = require "Unit.ViewControl"
local ModeSelector = require "catchall.ModeSelector"
local SferaCutoffControl = require "catchall.SferaCutoffControl"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local paramMap = floatMap(0, 1)
local levelMap = floatMap(0, 2)
local spinMap = (function()
  local m = app.LinearDialMap(-2, 2)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end)()

local cutoffMap = (function()
  local m = app.LinearDialMap(20, 20000)
  m:setSteps(1000, 100, 10, 1)
  return m
end)()

local Sfera = Class {}
Sfera:include(Unit)

function Sfera:init(args)
  args.title = "Sfera"
  args.mnemonic = "Sf"
  Unit.init(self, args)
end

function Sfera:onLoadGraph(channelCount)
  local op = self:addObject("op", libcatchall.Sfera())

  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")

  if channelCount > 1 then
    local opR = self:addObject("opR", libcatchall.Sfera())
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
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")
  if stereo then connect(tune, "Out", self.objects.opR, "V/Oct") end

  -- Config
  local config = self:addObject("config", app.ParameterAdapter())
  config:hardSet("Bias", 0.0)
  tieParam("Config", config)
  self:addMonoBranch("config", config, "In", config, "Out")

  -- Param X
  local paramX = self:addObject("paramX", app.ParameterAdapter())
  paramX:hardSet("Bias", 0.5)
  tieParam("ParamX", paramX)
  self:addMonoBranch("paramX", paramX, "In", paramX, "Out")

  -- Param Y
  local paramY = self:addObject("paramY", app.ParameterAdapter())
  paramY:hardSet("Bias", 0.5)
  tieParam("ParamY", paramY)
  self:addMonoBranch("paramY", paramY, "In", paramY, "Out")

  -- Cutoff
  local cutoff = self:addObject("cutoff", app.ParameterAdapter())
  cutoff:hardSet("Bias", 1000.0)
  tieParam("Cutoff", cutoff)
  self:addMonoBranch("cutoff", cutoff, "In", cutoff, "Out")

  -- Q Scale
  local qScale = self:addObject("qScale", app.ParameterAdapter())
  qScale:hardSet("Bias", 1.0)
  tieParam("QScale", qScale)
  self:addMonoBranch("qScale", qScale, "In", qScale, "Out")

  -- Level
  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Bias", 1.0)
  tieParam("Level", level)
  self:addMonoBranch("level", level, "In", level, "Out")

  -- Spin
  local spin = self:addObject("spin", app.ParameterAdapter())
  spin:hardSet("Bias", 0.0)
  tieParam("Spin", spin)
  self:addMonoBranch("spin", spin, "In", spin, "Out")

  -- V/Oct branch
  self:addMonoBranch("tune", tune, "In", tune, "Out")
end

function Sfera:onLoadViews(objects, branches)
  -- Build config names from cube data
  local numCubes = objects.op:getNumCubes()
  local configMap = (function()
    local m = app.LinearDialMap(0, numCubes - 1)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  -- Config names: first 32 are hand-curated, rest are "gen"
  local configNames = {}
  local cubeNames = {
    "BW LP>HP", "BW LP>BP", "BW 2>6p", "BP>Ntch", "BW Quad", "BW Fade", "BW Ring", "BW Deep",
    "Moog Q", "Moog>BW", "Moog Sw", "Moog Rng",
    "AEIO", "AUOI", "EIUA", "Sop>Bas", "B>S AE", "B>S IO", "Vox Dia", "Vox Rev",
    "Comb", "Cmb>Flt", "Cmb Shf", "Cmb>Res",
    "Phase", "Phs>BW", "Phs>Res", "Phs Osc",
    "Reson", "Res>Flt", "Res>Vox", "Res>Cmb"
  }
  for i = 0, numCubes - 1 do
    if i < #cubeNames then
      configNames[i] = cubeNames[i + 1]
    else
      configNames[i] = "g" .. tostring(i)
    end
  end

  -- Sphere viz
  local vizView = Class {}
  vizView:include(ViewControl)

  function vizView:init(args)
    ViewControl.init(self)
    self:setClassName("Sfera.SphereView")
    local graphic = app.Graphic(0, 0, ply, 64)
    self:setMainCursorController(graphic)
    self:setControlGraphic(graphic)
    self:addSpotDescriptor{center = ply * 0.5}

    local sphere = libcatchall.SferaGraphic(0, 0, ply, 64)
    sphere:follow(args.dspObject)
    graphic:addChild(sphere)
  end

  return {
    config = ModeSelector {
      button = "config",
      description = "Config",
      branch = branches.config,
      gainbias = objects.config,
      range = objects.config,
      biasMap = configMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = configNames
    },
    viz = vizView {
      dspObject = objects.op
    },
    paramX = GainBias {
      button = "X",
      description = "Morph X",
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
      description = "Morph Y",
      branch = branches.paramY,
      gainbias = objects.paramY,
      range = objects.paramY,
      biasMap = paramMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    cutoff = SferaCutoffControl {
      button = "cutoff",
      description = "Cutoff",
      branch = branches.cutoff,
      gainbias = objects.cutoff,
      range = objects.cutoff,
      biasMap = cutoffMap,
      biasUnits = app.unitHertz,
      biasPrecision = 0,
      initialBias = 1000.0,
      qScale = objects.qScale:getParameter("Bias")
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
      initialBias = 1.0
    },
    spin = GainBias {
      button = "spin",
      description = "Spin",
      branch = branches.spin,
      gainbias = objects.spin,
      range = objects.spin,
      biasMap = spinMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    }
  }, {
    expanded = { "config", "viz", "paramX", "paramY", "cutoff", "spin", "level" },
    collapsed = {}
  }
end

return Sfera
