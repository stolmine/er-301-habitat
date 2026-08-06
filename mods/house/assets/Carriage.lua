-- Carriage -- fourth chain-as-unit in the house package, first
-- dynamics-character unit. Inverse-threshold engagement detector
-- drives Point transient injection and Distance2 air absorption.
-- Gets MORE active on flat material, LESS on dynamic material.
--
-- 5 plies: Drive / Reach / Form / Air / Mix. All continuous with
-- standard coarse/fine encoder stepping.
--   Drive  - Console0 input/output saturation gain
--   Reach  - engagement amount (couples Point boost + time scale)
--   Form   - envelope source: raw level (0) ↔ trajectory delta (1)
--   Air    - Distance2 absorption ceiling (engagement-scaled internally)
--   Mix    - dry/wet
--
-- Plan: planning/carriage-design.md
-- Implementation: atoms/Carriage.h (monolithic Object, uses
-- PointMono + Distance2Mono helpers).

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

local Carriage = Class {}
Carriage:include(Unit)

function Carriage:init(args)
  args.title = "Carriage"
  args.mnemonic = "Cg"
  Unit.init(self, args)
end

function Carriage:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.Carriage())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  local drive = self:addObject("drive", app.ParameterAdapter())
  drive:hardSet("Bias", 0.5)
  tie(op, "Drive", drive, "Out")
  self:addMonoBranch("drive", drive, "In", drive, "Out")

  local reach = self:addObject("reach", app.ParameterAdapter())
  reach:hardSet("Bias", 0.6)
  tie(op, "Reach", reach, "Out")
  self:addMonoBranch("reach", reach, "In", reach, "Out")

  local form = self:addObject("form", app.ParameterAdapter())
  form:hardSet("Bias", 0.4)
  tie(op, "Form", form, "Out")
  self:addMonoBranch("form", form, "In", form, "Out")

  local air = self:addObject("air", app.ParameterAdapter())
  air:hardSet("Bias", 0.5)
  tie(op, "Air", air, "Out")
  self:addMonoBranch("air", air, "In", air, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 1.0)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

function Carriage:onLoadViews()
  return {
    drive = GainBias {
      button = "drive",
      description = "Drive",
      branch = self.branches.drive,
      gainbias = self.objects.drive,
      range = self.objects.drive,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    reach = GainBias {
      button = "rch",
      description = "Reach",
      branch = self.branches.reach,
      gainbias = self.objects.reach,
      range = self.objects.reach,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.6
    },
    form = GainBias {
      button = "form",
      description = "Form",
      branch = self.branches.form,
      gainbias = self.objects.form,
      range = self.objects.form,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.4
    },
    air = GainBias {
      button = "air",
      description = "Air",
      branch = self.branches.air,
      gainbias = self.objects.air,
      range = self.objects.air,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    mix = GainBias {
      button = "mix",
      description = "Mix",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = Encoder.getMap("unit"),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    }
  }, {
    expanded = { "drive", "reach", "form", "air", "mix" },
    collapsed = {}
  }
end

return Carriage
