-- Filament -- third chain-as-unit in the house package, first
-- filter-character unit. Console0-wrapped Capacitor2 LP with
-- signal-voltage-modulated cutoff. The filter "breathes" with
-- input dynamics.
--
-- 5 plies: Drive / Cutoff / FM / Bloom / Mix. All continuous with
-- standard coarse/fine encoder stepping. Bloom adds allpass-in-
-- feedback "ghost resonance" — phase-mediated peak that smears
-- and shifts with the coupled APF coefficient (genuinely
-- different character from a Moog-style sharp peak).
--
-- Plan: planning/filament-port-plan.md
-- Implementation: atoms/Filament.h (monolithic Object, uses
-- Capacitor2Mono helper from atoms/Capacitor2.h)

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

local Filament = Class {}
Filament:include(Unit)

function Filament:init(args)
  args.title = "Filament"
  args.mnemonic = "Fl"
  Unit.init(self, args)
end

function Filament:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.Filament())

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

  local cutoff = self:addObject("cutoff", app.ParameterAdapter())
  cutoff:hardSet("Bias", 0.6)
  tie(op, "Cutoff", cutoff, "Out")
  self:addMonoBranch("cutoff", cutoff, "In", cutoff, "Out")

  local fm = self:addObject("fm", app.ParameterAdapter())
  fm:hardSet("Bias", 0.5)
  tie(op, "FM", fm, "Out")
  self:addMonoBranch("fm", fm, "In", fm, "Out")

  local bloom = self:addObject("bloom", app.ParameterAdapter())
  bloom:hardSet("Bias", 0.0)
  tie(op, "Bloom", bloom, "Out")
  self:addMonoBranch("bloom", bloom, "In", bloom, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 1.0)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

function Filament:onLoadViews()
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
    cutoff = GainBias {
      button = "cut",
      description = "Cutoff",
      branch = self.branches.cutoff,
      gainbias = self.objects.cutoff,
      range = self.objects.cutoff,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.6
    },
    fm = GainBias {
      button = "fm",
      description = "FM",
      branch = self.branches.fm,
      gainbias = self.objects.fm,
      range = self.objects.fm,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    bloom = GainBias {
      button = "blom",
      description = "Bloom",
      branch = self.branches.bloom,
      gainbias = self.objects.bloom,
      range = self.objects.bloom,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
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
    expanded = { "drive", "cutoff", "fm", "bloom", "mix" },
    collapsed = {}
  }
end

return Filament
