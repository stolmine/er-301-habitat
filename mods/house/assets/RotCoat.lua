-- RotCoat -- first original-design reverb in the house package.
-- Multi-world 4x4 FDN where each line lives at its own
-- undersample rate. Mulch fans the per-line rates around a base
-- World; Householder cross-feeds the four worlds into one wash.
-- Five params: World / Regen / Predelay / Mulch / Wetness.
--
-- "RotCoat" is the working codename; final habitat-native name
-- (Lath / Cure / Sediment / Patina / other) to be locked after
-- hardware audition.
--
-- See atoms/RotCoat.h header for hybrid float allocation +
-- design rationale. Plan: planning/rotcoat-port-plan.md.

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

local RotCoat = Class {}
RotCoat:include(Unit)

function RotCoat:init(args)
  args.title = "RotCoat"
  args.mnemonic = "Rc"
  Unit.init(self, args)
end

function RotCoat:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.RotCoat())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  local world = self:addObject("world", app.ParameterAdapter())
  world:hardSet("Bias", 0.5)
  tie(op, "World", world, "Out")
  self:addMonoBranch("world", world, "In", world, "Out")

  local regen = self:addObject("regen", app.ParameterAdapter())
  regen:hardSet("Bias", 0.5)
  tie(op, "Regen", regen, "Out")
  self:addMonoBranch("regen", regen, "In", regen, "Out")

  local predelay = self:addObject("predelay", app.ParameterAdapter())
  predelay:hardSet("Bias", 0.0)
  tie(op, "Predelay", predelay, "Out")
  self:addMonoBranch("predelay", predelay, "In", predelay, "Out")

  local mulch = self:addObject("mulch", app.ParameterAdapter())
  mulch:hardSet("Bias", 0.0)
  tie(op, "Mulch", mulch, "Out")
  self:addMonoBranch("mulch", mulch, "In", mulch, "Out")

  local wetness = self:addObject("wetness", app.ParameterAdapter())
  wetness:hardSet("Bias", 0.5)
  tie(op, "Wetness", wetness, "Out")
  self:addMonoBranch("wetness", wetness, "In", wetness, "Out")
end

function RotCoat:onLoadViews()
  return {
    world = GainBias {
      button = "world",
      description = "World",
      branch = self.branches.world,
      gainbias = self.objects.world,
      range = self.objects.world,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    regen = GainBias {
      button = "regen",
      description = "Regen",
      branch = self.branches.regen,
      gainbias = self.objects.regen,
      range = self.objects.regen,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    predelay = GainBias {
      button = "pdly",
      description = "Predelay",
      branch = self.branches.predelay,
      gainbias = self.objects.predelay,
      range = self.objects.predelay,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    mulch = GainBias {
      button = "mulch",
      description = "Mulch",
      branch = self.branches.mulch,
      gainbias = self.objects.mulch,
      range = self.objects.mulch,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
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
      initialBias = 0.5
    }
  }, {
    expanded = { "world", "regen", "predelay", "mulch", "wetness" },
    collapsed = {}
  }
end

return RotCoat
