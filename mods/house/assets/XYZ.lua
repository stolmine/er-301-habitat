-- XYZ -- second original-design reverb in the house package.
-- Cryptic 3-axis engine: X (size + APF↔delay morph), Y (sat +
-- undersample coupled-curve), Z (topology switch Nested/Folded/
-- Coupled). 5 plies: X / Y / Z / Predelay / Wetness.
--
-- "XYZ" is the working codename; final habitat-native name
-- (Cistern / Vault / Crypt / Reliquary / Ley / other) to be
-- locked after hardware audition.
--
-- See atoms/XYZ.h for the full design + math.
-- Plan: planning/xyz-port-plan.md.

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

local XYZ = Class {}
XYZ:include(Unit)

function XYZ:init(args)
  args.title = "XYZ"
  args.mnemonic = "Xy"
  Unit.init(self, args)
end

function XYZ:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.XYZ())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  local x = self:addObject("x", app.ParameterAdapter())
  x:hardSet("Bias", 0.5)
  tie(op, "X", x, "Out")
  self:addMonoBranch("x", x, "In", x, "Out")

  local y = self:addObject("y", app.ParameterAdapter())
  y:hardSet("Bias", 0.3)
  tie(op, "Y", y, "Out")
  self:addMonoBranch("y", y, "In", y, "Out")

  local z = self:addObject("z", app.ParameterAdapter())
  z:hardSet("Bias", 0.5)
  tie(op, "Z", z, "Out")
  self:addMonoBranch("z", z, "In", z, "Out")

  local predelay = self:addObject("predelay", app.ParameterAdapter())
  predelay:hardSet("Bias", 0.0)
  tie(op, "Predelay", predelay, "Out")
  self:addMonoBranch("predelay", predelay, "In", predelay, "Out")

  local wetness = self:addObject("wetness", app.ParameterAdapter())
  wetness:hardSet("Bias", 0.5)
  tie(op, "Wetness", wetness, "Out")
  self:addMonoBranch("wetness", wetness, "In", wetness, "Out")
end

function XYZ:onLoadViews()
  return {
    x = GainBias {
      button = "x",
      description = "X",
      branch = self.branches.x,
      gainbias = self.objects.x,
      range = self.objects.x,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    y = GainBias {
      button = "y",
      description = "Y",
      branch = self.branches.y,
      gainbias = self.objects.y,
      range = self.objects.y,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.3
    },
    z = GainBias {
      button = "z",
      description = "Z",
      branch = self.branches.z,
      gainbias = self.objects.z,
      range = self.objects.z,
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
    expanded = { "x", "y", "z", "predelay", "wetness" },
    collapsed = {}
  }
end

return XYZ
