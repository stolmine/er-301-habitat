-- Verbity -- Airwindows Verbity port. 3-stage cascaded 4x4 FDN
-- with input+output IIR lowpass, per-feedback-tap interpolation
-- smoother, and a sub-low "thunder" chase. 4 params: Bigness /
-- Longness / Darkness / Wetness. Submix wet/dry (Wetness=0.5 sums
-- full wet AND full dry, send-style).
--
-- Phase 1 hybrid float -- see atoms/Verbity.h header.
-- Plan: planning/verbity-port-plan.md.

local app = app
local libhouse = require "house.libhouse"
local Encoder = require "Encoder"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"

local floatMap = function(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local zeroOneMap = floatMap(0, 1)

local Verbity = Class {}
Verbity:include(Unit)

function Verbity:init(args)
  args.title = "Verbity"
  args.mnemonic = "Vt"
  Unit.init(self, args)
end

function Verbity:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.Verbity())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  local bigness = self:addObject("bigness", app.ParameterAdapter())
  bigness:hardSet("Bias", 0.25)
  tie(op, "Bigness", bigness, "Out")
  self:addMonoBranch("bigness", bigness, "In", bigness, "Out")

  local longness = self:addObject("longness", app.ParameterAdapter())
  longness:hardSet("Bias", 0.0)
  tie(op, "Longness", longness, "Out")
  self:addMonoBranch("longness", longness, "In", longness, "Out")

  local darkness = self:addObject("darkness", app.ParameterAdapter())
  darkness:hardSet("Bias", 0.25)
  tie(op, "Darkness", darkness, "Out")
  self:addMonoBranch("darkness", darkness, "In", darkness, "Out")

  local wetness = self:addObject("wetness", app.ParameterAdapter())
  wetness:hardSet("Bias", 0.25)
  tie(op, "Wetness", wetness, "Out")
  self:addMonoBranch("wetness", wetness, "In", wetness, "Out")
end

function Verbity:onLoadViews()
  return {
    bigness = GainBias {
      button = "big",
      description = "Bigness",
      branch = self.branches.bigness,
      gainbias = self.objects.bigness,
      range = self.objects.bigness,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.25
    },
    longness = GainBias {
      button = "long",
      description = "Longness",
      branch = self.branches.longness,
      gainbias = self.objects.longness,
      range = self.objects.longness,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    darkness = GainBias {
      button = "dark",
      description = "Darkness",
      branch = self.branches.darkness,
      gainbias = self.objects.darkness,
      range = self.objects.darkness,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.25
    },
    wetness = GainBias {
      button = "wet",
      description = "Wetness",
      branch = self.branches.wetness,
      gainbias = self.objects.wetness,
      range = self.objects.wetness,
      biasMap = Encoder.getMap("unit"),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.25
    }
  }, {
    expanded = { "bigness", "longness", "darkness", "wetness" },
    collapsed = {}
  }
end

return Verbity
