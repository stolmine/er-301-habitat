-- CreamCoat -- Airwindows CreamCoat port. Bright-ambience engine
-- with the canonical divisor+Bezier mechanic exposed as a user
-- knob (DeRez). 5 parameters: Select / Regen / DeRez / Predlay /
-- Wetness. Submix-style wet/dry (Wetness=0.5 sums full wet AND
-- full dry, suited for sends).
--
-- Phase 1 hybrid float-conversion -- see atoms/CreamCoat.h
-- header for rationale.
--
-- Plan: planning/creamcoat-port-plan.md.

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

local CreamCoat = Class {}
CreamCoat:include(Unit)

function CreamCoat:init(args)
  args.title = "CreamCoat"
  args.mnemonic = "Cc"
  Unit.init(self, args)
end

function CreamCoat:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.CreamCoat())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  -- 5 ParameterAdapter ties.
  local select = self:addObject("select", app.ParameterAdapter())
  select:hardSet("Bias", 0.5)
  tie(op, "Select", select, "Out")
  self:addMonoBranch("select", select, "In", select, "Out")

  local regen = self:addObject("regen", app.ParameterAdapter())
  regen:hardSet("Bias", 0.5)
  tie(op, "Regen", regen, "Out")
  self:addMonoBranch("regen", regen, "In", regen, "Out")

  local derez = self:addObject("derez", app.ParameterAdapter())
  derez:hardSet("Bias", 1.0)
  tie(op, "DeRez", derez, "Out")
  self:addMonoBranch("derez", derez, "In", derez, "Out")

  local predlay = self:addObject("predlay", app.ParameterAdapter())
  predlay:hardSet("Bias", 0.0)
  tie(op, "Predlay", predlay, "Out")
  self:addMonoBranch("predlay", predlay, "In", predlay, "Out")

  local wetness = self:addObject("wetness", app.ParameterAdapter())
  wetness:hardSet("Bias", 0.25)
  tie(op, "Wetness", wetness, "Out")
  self:addMonoBranch("wetness", wetness, "In", wetness, "Out")
end

function CreamCoat:onLoadViews()
  return {
    select = GainBias {
      button = "box",
      description = "Box (1 of 17)",
      branch = self.branches.select,
      gainbias = self.objects.select,
      range = self.objects.select,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    regen = GainBias {
      button = "regen",
      description = "Regeneration",
      branch = self.branches.regen,
      gainbias = self.objects.regen,
      range = self.objects.regen,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    derez = GainBias {
      button = "derez",
      description = "DeRez (lush/cheap)",
      branch = self.branches.derez,
      gainbias = self.objects.derez,
      range = self.objects.derez,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    predlay = GainBias {
      button = "pdly",
      description = "Predelay",
      branch = self.branches.predlay,
      gainbias = self.objects.predlay,
      range = self.objects.predlay,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    wetness = GainBias {
      button = "wet",
      description = "Wetness (submix 0.5=full+full)",
      branch = self.branches.wetness,
      gainbias = self.objects.wetness,
      range = self.objects.wetness,
      biasMap = Encoder.getMap("unit"),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.25
    }
  }, {
    expanded = { "select", "regen", "derez", "predlay", "wetness" },
    collapsed = {}
  }
end

return CreamCoat
