local app = app
local libcatchall = require "biome.libcatchall"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Encoder = require "Encoder"

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local depthMap = floatMap(0, 1)
local delayMap = floatMap(0, 1)
local warbleMap = floatMap(0, 1)
local noiseMap = floatMap(0, 1)
local dryWetMap = floatMap(0, 1)

local Flakes = Class {}
Flakes:include(Unit)

function Flakes:init(args)
  args.title = "Flakes"
  args.mnemonic = "Fk"
  Unit.init(self, args)
end

function Flakes:onLoadGraph(channelCount)
  local op = self:addObject("op", libcatchall.Flakes())
  op:allocateTimeUpTo(10.0)

  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")

  if channelCount > 1 then
    local opR = self:addObject("opR", libcatchall.Flakes())
    opR:allocateTimeUpTo(10.0)
    connect(self, "In2", opR, "In")
    connect(opR, "Out", self, "Out2")
  end

  local stereo = channelCount > 1

  local function tieParam(name, adapter)
    tie(op, name, adapter, "Out")
    if stereo then tie(self.objects.opR, name, adapter, "Out") end
  end

  -- Freeze gate
  local freezeGate = self:addObject("freeze", app.Comparator())
  freezeGate:setGateMode()
  connect(freezeGate, "Out", op, "Freeze")
  if stereo then connect(freezeGate, "Out", self.objects.opR, "Freeze") end
  self:addMonoBranch("freeze", freezeGate, "In", freezeGate, "Out")

  -- Depth
  local depth = self:addObject("depth", app.ParameterAdapter())
  depth:hardSet("Bias", 0.5)
  tieParam("Depth", depth)
  self:addMonoBranch("depth", depth, "In", depth, "Out")

  -- Delay
  local delay = self:addObject("delay", app.ParameterAdapter())
  delay:hardSet("Bias", 0.25)
  tieParam("Delay", delay)
  self:addMonoBranch("delay", delay, "In", delay, "Out")

  -- Warble
  local warble = self:addObject("warble", app.ParameterAdapter())
  warble:hardSet("Bias", 0.24)
  tieParam("Warble", warble)
  self:addMonoBranch("warble", warble, "In", warble, "Out")

  -- Noise
  local noiseParam = self:addObject("noise", app.ParameterAdapter())
  noiseParam:hardSet("Bias", 0.1)
  tieParam("Noise", noiseParam)
  self:addMonoBranch("noise", noiseParam, "In", noiseParam, "Out")

  -- Dry/Wet
  local dryWet = self:addObject("dryWet", app.ParameterAdapter())
  dryWet:hardSet("Bias", 0.5)
  tieParam("DryWet", dryWet)
  self:addMonoBranch("dryWet", dryWet, "In", dryWet, "Out")
end

function Flakes:onLoadViews(objects, branches)
  return {
    freeze = Gate {
      button = "freeze",
      description = "Freeze",
      branch = branches.freeze,
      comparator = objects.freeze
    },
    depth = GainBias {
      button = "depth",
      description = "Depth",
      branch = branches.depth,
      gainbias = objects.depth,
      range = objects.depth,
      biasMap = depthMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    delay = GainBias {
      button = "delay",
      description = "Delay",
      branch = branches.delay,
      gainbias = objects.delay,
      range = objects.delay,
      biasMap = delayMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.25
    },
    warble = GainBias {
      button = "warble",
      description = "Warble",
      branch = branches.warble,
      gainbias = objects.warble,
      range = objects.warble,
      biasMap = warbleMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.24
    },
    noise = GainBias {
      button = "noise",
      description = "Noise",
      branch = branches.noise,
      gainbias = objects.noise,
      range = objects.noise,
      biasMap = noiseMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.1
    },
    dryWet = GainBias {
      button = "d/w",
      description = "Dry/Wet",
      branch = branches.dryWet,
      gainbias = objects.dryWet,
      range = objects.dryWet,
      biasMap = dryWetMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    }
  }, {
    expanded = { "freeze", "depth", "delay", "warble", "noise", "dryWet" },
    collapsed = {}
  }
end

return Flakes
