local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local NetworkOverviewControl = require "spreadsheet.NetworkOverviewControl"

-- Network — non-traditional reverb / "macro spatial simulation".
-- Phase 1: 32-tap stereo with virtual-reflector geometry, listener
-- motion on a circular orbit, per-tap pan derived from azimuth.
--
-- See planning/network-implementation-plan.md.

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local sizeMap = floatMap(0, 1)
local densityMap = floatMap(0, 1)
local motionMap = floatMap(0, 1)
local connectivityMap = floatMap(0, 1)
local decayMap = floatMap(0, 1)
local glitchMap = floatMap(0, 1)
local wetMap = floatMap(0, 1)
local seedMap = (function()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.0001)
  return m
end)()

local Network = Class {}
Network:include(Unit)

function Network:init(args)
  args.title = "Network"
  args.mnemonic = "Nw"
  Unit.init(self, args)
end

function Network:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Network())
  -- Allocate up to 1 second of delay buffer.
  op:allocateTimeUpTo(1.0)

  -- Mono in → stereo out (Pecto / Petrichor pattern).
  -- The unit's process() writes Out (left) + Out2 (right) directly
  -- via per-tap pan derived from reflector azimuth.
  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "OutR", self, "Out2")
  end

  local function tieParam(name, adapter)
    tie(op, name, adapter, "Out")
  end

  -- Size
  local size = self:addObject("size", app.ParameterAdapter())
  size:hardSet("Bias", 0.5)
  tieParam("Size", size)
  self:addMonoBranch("size", size, "In", size, "Out")

  -- Density
  local densityCtl = self:addObject("density", app.ParameterAdapter())
  densityCtl:hardSet("Bias", 0.5)
  tieParam("Density", densityCtl)
  self:addMonoBranch("density", densityCtl, "In", densityCtl, "Out")

  -- Motion
  local motion = self:addObject("motion", app.ParameterAdapter())
  motion:hardSet("Bias", 0.0)
  tieParam("Motion", motion)
  self:addMonoBranch("motion", motion, "In", motion, "Out")

  -- Connectivity
  local connectivity = self:addObject("connectivity", app.ParameterAdapter())
  connectivity:hardSet("Bias", 0.0)
  tieParam("Connectivity", connectivity)
  self:addMonoBranch("connectivity", connectivity, "In", connectivity, "Out")

  -- Decay
  local decay = self:addObject("decay", app.ParameterAdapter())
  decay:hardSet("Bias", 0.5)
  tieParam("Decay", decay)
  self:addMonoBranch("decay", decay, "In", decay, "Out")

  -- Glitch (Character macro — lush↔glitch)
  local glitch = self:addObject("glitch", app.ParameterAdapter())
  glitch:hardSet("Bias", 0.0)
  tieParam("Glitch", glitch)
  self:addMonoBranch("glitch", glitch, "In", glitch, "Out")

  -- Wet
  local wet = self:addObject("wet", app.ParameterAdapter())
  wet:hardSet("Bias", 0.5)
  tieParam("Wet", wet)
  self:addMonoBranch("wet", wet, "In", wet, "Out")

  -- Seed (no CV branch — set-once via encoder; reflector field
  -- regenerates whenever the value changes).
  local seed = self:addObject("seed", app.ParameterAdapter())
  seed:hardSet("Bias", 0.0)
  tieParam("Seed", seed)
end

function Network:onLoadViews(objects, branches)
  return {
    size = GainBias {
      button = "size",
      description = "Size",
      branch = branches.size,
      gainbias = objects.size,
      range = objects.size,
      biasMap = sizeMap,
      biasUnits = app.unitNone,
      biasPrecision = 3,
      initialBias = 0.5
    },
    density = GainBias {
      button = "dens",
      description = "Density",
      branch = branches.density,
      gainbias = objects.density,
      range = objects.density,
      biasMap = densityMap,
      biasUnits = app.unitNone,
      biasPrecision = 3,
      initialBias = 0.5
    },
    motion = GainBias {
      button = "motn",
      description = "Motion",
      branch = branches.motion,
      gainbias = objects.motion,
      range = objects.motion,
      biasMap = motionMap,
      biasUnits = app.unitNone,
      biasPrecision = 3,
      initialBias = 0.5
    },
    connectivity = GainBias {
      button = "conn",
      description = "Connectivity",
      branch = branches.connectivity,
      gainbias = objects.connectivity,
      range = objects.connectivity,
      biasMap = connectivityMap,
      biasUnits = app.unitNone,
      biasPrecision = 3,
      initialBias = 0.0
    },
    decay = GainBias {
      button = "decay",
      description = "Decay",
      branch = branches.decay,
      gainbias = objects.decay,
      range = objects.decay,
      biasMap = decayMap,
      biasUnits = app.unitNone,
      biasPrecision = 3,
      initialBias = 0.5
    },
    glitch = NetworkOverviewControl {
      button = "gltch",
      description = "Glitch",
      branch = branches.glitch,
      gainbias = objects.glitch,
      range = objects.glitch,
      biasMap = glitchMap,
      biasUnits = app.unitNone,
      biasPrecision = 3,
      initialBias = 0.0,
      op = objects.op
    },
    wet = GainBias {
      button = "wet",
      description = "Dry/Wet mix",
      branch = branches.wet,
      gainbias = objects.wet,
      range = objects.wet,
      biasMap = wetMap,
      biasUnits = app.unitNone,
      biasPrecision = 3,
      initialBias = 0.5
    }
  }, {
    expanded = { "glitch", "size", "density", "motion", "connectivity", "decay", "wet" },
    collapsed = {}
  }
end

return Network
