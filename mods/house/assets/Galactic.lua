-- Galactic -- Airwindows Galactic port. 3-stage cascaded 4x4 FDN
-- with 256-sample modulated predelay (LFO vibrato) and full L<->R
-- cross-coupling at the feedback stage. The lush option. 5 params:
-- Replace / Brightness / Detune / BigDim / DryWet. Standard
-- crossfade wet/dry. AW defaults are D=BigDim=1.0 and E=DryWet=1.0
-- (max size, full wet) -- preserved per faithful-port discipline.
--
-- Phase 1 hybrid float -- see atoms/Galactic.h header.
-- Plan: planning/galactic-port-plan.md.

local app = app
local libhouse = require "house.libhouse"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"

local floatMap = function(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local zeroOneMap = floatMap(0, 1)

local Galactic = Class {}
Galactic:include(Unit)

function Galactic:init(args)
  args.title = "Galactic"
  args.mnemonic = "Gx"
  Unit.init(self, args)
end

function Galactic:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.Galactic())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  local replace = self:addObject("replace", app.ParameterAdapter())
  replace:hardSet("Bias", 0.5)
  tie(op, "Replace", replace, "Out")
  self:addMonoBranch("replace", replace, "In", replace, "Out")

  local brightness = self:addObject("brightness", app.ParameterAdapter())
  brightness:hardSet("Bias", 0.5)
  tie(op, "Brightness", brightness, "Out")
  self:addMonoBranch("brightness", brightness, "In", brightness, "Out")

  local detune = self:addObject("detune", app.ParameterAdapter())
  detune:hardSet("Bias", 0.5)
  tie(op, "Detune", detune, "Out")
  self:addMonoBranch("detune", detune, "In", detune, "Out")

  local bigdim = self:addObject("bigdim", app.ParameterAdapter())
  bigdim:hardSet("Bias", 1.0)
  tie(op, "BigDim", bigdim, "Out")
  self:addMonoBranch("bigdim", bigdim, "In", bigdim, "Out")

  local drywet = self:addObject("drywet", app.ParameterAdapter())
  drywet:hardSet("Bias", 1.0)
  tie(op, "DryWet", drywet, "Out")
  self:addMonoBranch("drywet", drywet, "In", drywet, "Out")
end

function Galactic:onLoadViews()
  return {
    replace = GainBias {
      button = "rep",
      description = "Replace",
      branch = self.branches.replace,
      gainbias = self.objects.replace,
      range = self.objects.replace,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    brightness = GainBias {
      button = "bri",
      description = "Brightness",
      branch = self.branches.brightness,
      gainbias = self.objects.brightness,
      range = self.objects.brightness,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    detune = GainBias {
      button = "det",
      description = "Detune",
      branch = self.branches.detune,
      gainbias = self.objects.detune,
      range = self.objects.detune,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    bigdim = GainBias {
      button = "big",
      description = "BigDim",
      branch = self.branches.bigdim,
      gainbias = self.objects.bigdim,
      range = self.objects.bigdim,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    drywet = GainBias {
      button = "wet",
      description = "DryWet",
      branch = self.branches.drywet,
      gainbias = self.objects.drywet,
      range = self.objects.drywet,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    }
  }, {
    expanded = { "replace", "brightness", "detune", "bigdim", "drywet" },
    collapsed = {}
  }
end

return Galactic
