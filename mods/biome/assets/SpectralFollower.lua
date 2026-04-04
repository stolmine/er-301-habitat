local app = app
local libstolmine = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local SpectralFollower = Class {}
SpectralFollower:include(Unit)

function SpectralFollower:init(args)
  args.title = "Spectral Follower"
  args.mnemonic = "SF"
  Unit.init(self, args)
end

function SpectralFollower:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.SpectralFollower())
  local freq = self:addObject("freq", app.ParameterAdapter())
  local bandwidth = self:addObject("bandwidth", app.ParameterAdapter())
  local attack = self:addObject("attack", app.ParameterAdapter())
  local decay = self:addObject("decay", app.ParameterAdapter())

  freq:hardSet("Bias", 1000.0)
  bandwidth:hardSet("Bias", 1.0)
  attack:hardSet("Bias", 0.005)
  decay:hardSet("Bias", 0.050)

  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  tie(op, "Freq", freq, "Out")
  tie(op, "Bandwidth", bandwidth, "Out")
  tie(op, "Attack", attack, "Out")
  tie(op, "Decay", decay, "Out")

  self:addMonoBranch("freq", freq, "In", freq, "Out")
  self:addMonoBranch("bandwidth", bandwidth, "In", bandwidth, "Out")
  self:addMonoBranch("attack", attack, "In", attack, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")
end

local views = {
  expanded = { "freq", "bandwidth", "attack", "decay" },
  collapsed = {}
}

local function freqMap()
  local m = app.LinearDialMap(20, 20000)
  m:setSteps(1000, 100, 10, 1)
  return m
end

local function bwMap()
  local m = app.LinearDialMap(0.1, 4)
  m:setSteps(1, 0.5, 0.1, 0.01)
  return m
end

local function attackMap()
  local m = app.LinearDialMap(0.0001, 0.5)
  m:setSteps(0.1, 0.01, 0.001, 0.0001)
  return m
end

local function decayMap()
  local m = app.LinearDialMap(0.0001, 5.0)
  m:setSteps(0.5, 0.1, 0.01, 0.001)
  return m
end

function SpectralFollower:onLoadViews(objects, branches)
  local controls = {}

  controls.freq = GainBias {
    button = "freq",
    branch = branches.freq,
    description = "Center Freq",
    gainbias = objects.freq,
    range = objects.freq,
    biasMap = freqMap(),
    biasUnits = app.unitHertz,
    biasPrecision = 0,
    initialBias = 1000.0
  }

  controls.bandwidth = GainBias {
    button = "bw",
    branch = branches.bandwidth,
    description = "Bandwidth",
    gainbias = objects.bandwidth,
    range = objects.bandwidth,
    biasMap = bwMap(),
    biasUnits = app.unitNone,
    biasPrecision = 1,
    initialBias = 1.0
  }

  controls.attack = GainBias {
    button = "atk",
    branch = branches.attack,
    description = "Attack",
    gainbias = objects.attack,
    range = objects.attack,
    biasMap = attackMap(),
    biasUnits = app.unitSecs,
    biasPrecision = 3,
    initialBias = 0.005
  }

  controls.decay = GainBias {
    button = "dec",
    branch = branches.decay,
    description = "Decay",
    gainbias = objects.decay,
    range = objects.decay,
    biasMap = decayMap(),
    biasUnits = app.unitSecs,
    biasPrecision = 3,
    initialBias = 0.050
  }

  return controls, views
end

return SpectralFollower
