-- BrightAmbience3 -- Airwindows BrightAmbience3 port. Sparse-
-- prime-tap delay summation with resonant SVF feedback. Bright,
-- gated halo character. 4 params: Position / Size / Brightness /
-- Wetness. Size is the headline CPU dial (sums up to 487 sparse
-- taps per channel per cycle at Size=1.0).
--
-- Phase 1 hybrid float -- see atoms/BrightAmbience3.h header.
-- Plan: planning/brightambience3-port-plan.md.

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

local BrightAmbience3 = Class {}
BrightAmbience3:include(Unit)

function BrightAmbience3:init(args)
  args.title = "BrightAmbience3"
  args.mnemonic = "Ba"
  Unit.init(self, args)
end

function BrightAmbience3:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.BrightAmbience3())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  local position = self:addObject("position", app.ParameterAdapter())
  position:hardSet("Bias", 0.5)
  tie(op, "Position", position, "Out")
  self:addMonoBranch("position", position, "In", position, "Out")

  local size = self:addObject("size", app.ParameterAdapter())
  size:hardSet("Bias", 0.5)
  tie(op, "Size", size, "Out")
  self:addMonoBranch("size", size, "In", size, "Out")

  local brightness = self:addObject("brightness", app.ParameterAdapter())
  brightness:hardSet("Bias", 0.5)
  tie(op, "Brightness", brightness, "Out")
  self:addMonoBranch("brightness", brightness, "In", brightness, "Out")

  local wetness = self:addObject("wetness", app.ParameterAdapter())
  wetness:hardSet("Bias", 0.5)
  tie(op, "Wetness", wetness, "Out")
  self:addMonoBranch("wetness", wetness, "In", wetness, "Out")
end

function BrightAmbience3:onLoadViews()
  return {
    position = GainBias {
      button = "pos",
      description = "Position",
      branch = self.branches.position,
      gainbias = self.objects.position,
      range = self.objects.position,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    size = GainBias {
      button = "size",
      description = "Size",
      branch = self.branches.size,
      gainbias = self.objects.size,
      range = self.objects.size,
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
    wetness = GainBias {
      button = "wet",
      description = "Wetness",
      branch = self.branches.wetness,
      gainbias = self.objects.wetness,
      range = self.objects.wetness,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    }
  }, {
    expanded = { "position", "size", "brightness", "wetness" },
    collapsed = {}
  }
end

return BrightAmbience3
