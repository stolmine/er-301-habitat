local app = app
local libcatchall = require "catchall.libcatchall"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local ViewControl = require "Unit.ViewControl"
local ModeSelector = require "catchall.ModeSelector"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local scanMap = floatMap(0, 1)
local levelMap = floatMap(0, 1)

local f0Map = (function()
  local m = app.LinearDialMap(0.1, 2000)
  m:setSteps(100, 10, 1, 0.1)
  return m
end)()

local cutoffMap = (function()
  local m = app.LinearDialMap(20, 20000)
  m:setSteps(1000, 100, 10, 1)
  return m
end)()

local seedMap = (function()
  local m = app.LinearDialMap(0, 999)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local seedNames = {}
for i = 0, 999 do
  seedNames[i] = tostring(i)
end

local Lambda = Class {}
Lambda:include(Unit)

function Lambda:init(args)
  args.title = "Lambda"
  args.mnemonic = "Lm"
  Unit.init(self, args)
end

function Lambda:onLoadGraph(channelCount)
  local op = self:addObject("op", libcatchall.Lambda())

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

  -- Seed
  local seed = self:addObject("seed", app.ParameterAdapter())
  seed:hardSet("Bias", 0.0)
  tie(op, "Seed", seed, "Out")
  self:addMonoBranch("seed", seed, "In", seed, "Out")

  -- Scan
  local scan = self:addObject("scan", app.ParameterAdapter())
  scan:hardSet("Bias", 0.0)
  tie(op, "Scan", scan, "Out")
  self:addMonoBranch("scan", scan, "In", scan, "Out")

  -- Fundamental
  local f0 = self:addObject("f0", app.ParameterAdapter())
  f0:hardSet("Bias", 110.0)
  tie(op, "Fundamental", f0, "Out")
  self:addMonoBranch("f0", f0, "In", f0, "Out")

  -- Cutoff
  local cutoff = self:addObject("cutoff", app.ParameterAdapter())
  cutoff:hardSet("Bias", 1000.0)
  tie(op, "Cutoff", cutoff, "Out")
  self:addMonoBranch("cutoff", cutoff, "In", cutoff, "Out")

  -- Level
  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Bias", 0.5)
  tie(op, "Level", level, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")

  -- V/Oct branch
  self:addMonoBranch("tune", tune, "In", tune, "Out")
end

function Lambda:onLoadViews(objects, branches)
  -- Waveform viz
  local vizView = Class {}
  vizView:include(ViewControl)

  function vizView:init(args)
    ViewControl.init(self)
    self:setClassName("Lambda.WaveView")
    local graphic = app.Graphic(0, 0, ply, 64)
    self:setMainCursorController(graphic)
    self:setControlGraphic(graphic)
    self:addSpotDescriptor{center = ply * 0.5}

    local wave = libcatchall.LambdaWaveGraphic(0, 0, ply, 64)
    wave:follow(args.dspObject)
    graphic:addChild(wave)
  end

  return {
    seed = ModeSelector {
      button = "seed",
      description = "Seed",
      branch = branches.seed,
      gainbias = objects.seed,
      range = objects.seed,
      biasMap = seedMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = seedNames
    },
    viz = vizView {
      dspObject = objects.op
    },
    tune = Pitch {
      button = "V/Oct",
      description = "V/Oct",
      branch = branches.tune,
      offset = objects.tune,
      range = objects.tuneRange
    },
    scan = GainBias {
      button = "scan",
      description = "Scan",
      branch = branches.scan,
      gainbias = objects.scan,
      range = objects.scan,
      biasMap = scanMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    f0 = GainBias {
      button = "f0",
      description = "Fundamental",
      branch = branches.f0,
      gainbias = objects.f0,
      range = objects.f0,
      biasMap = f0Map,
      biasUnits = app.unitHertz,
      biasPrecision = 1,
      initialBias = 110.0
    },
    cutoff = GainBias {
      button = "cutoff",
      description = "Cutoff",
      branch = branches.cutoff,
      gainbias = objects.cutoff,
      range = objects.cutoff,
      biasMap = cutoffMap,
      biasUnits = app.unitHertz,
      biasPrecision = 0,
      initialBias = 1000.0
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
    }
  }, {
    expanded = { "seed", "viz", "tune", "scan", "f0", "cutoff", "level" },
    collapsed = {}
  }
end

return Lambda
