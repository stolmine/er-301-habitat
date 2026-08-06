-- Sujet — spectral fiction. The frequency-domain counterpart to Fabula.
-- An STFT-based spectral reverb using Spectral Magnitude Decay (SMD):
-- per-bin recursive magnitude accumulation with artificial phase synthesis.
-- Where Fabula is a believable room, Sujet does what no physical room can —
-- bins that refuse to decay, frozen spectra, frequency-staggered onsets.
--
-- Inherent latency: 1280 samples ≈ 26.7 ms (STFT N=1024, hop=256, 4× overlap).
-- This is absorbed into the Predelay readout. Mix=1 → pure STFT wet,
-- transparent-but-delayed (the emu gate for sub-phase 0.2.0.1).
--
-- Sub-phase 0.2.0.1: STFT identity passthrough. Only Mix is DSP-wired.
-- Decay, Damp, Diffuse, Freeze, Blur, Bloom, Predelay are INERT stubs
-- (declared for a stable UI surface; wired progressively in 0.2.0.2+).
-- Design: planning/sujet-design.md §3/§7/§9.

local app = app
local libzaum = require "zaum.libzaum"
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

local Sujet = Class {}
Sujet:include(Unit)

function Sujet:init(args)
  args.title = "Sujet"
  args.mnemonic = "Su"
  Unit.init(self, args)
end

function Sujet:onLoadGraph(channelCount)
  local op = self:addObject("op", libzaum.STFTSpectral())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  else
    -- Mono fan-out: feed In1 to both STFT channels so Space can create stereo
    -- width from a mono source. Magnitude is identical L/R; Space decorrelates
    -- the phase anti-symmetrically → wide enveloping field from mono.
    connect(self, "In1", op, "In R")
  end

  local decay = self:addObject("decay", app.ParameterAdapter())
  decay:hardSet("Bias", 0.5)
  tie(op, "Decay", decay, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")

  local damp = self:addObject("damp", app.ParameterAdapter())
  damp:hardSet("Bias", 0.3)
  tie(op, "Damp", damp, "Out")
  self:addMonoBranch("damp", damp, "In", damp, "Out")

  local diffuse = self:addObject("diffuse", app.ParameterAdapter())
  diffuse:hardSet("Bias", 0.4)
  tie(op, "Diffuse", diffuse, "Out")
  self:addMonoBranch("diffuse", diffuse, "In", diffuse, "Out")

  local freeze = self:addObject("freeze", app.ParameterAdapter())
  freeze:hardSet("Bias", 0.0)
  tie(op, "Freeze", freeze, "Out")
  self:addMonoBranch("freeze", freeze, "In", freeze, "Out")

  local smear = self:addObject("smear", app.ParameterAdapter())
  smear:hardSet("Bias", 0.5)
  tie(op, "Smear", smear, "Out")
  self:addMonoBranch("smear", smear, "In", smear, "Out")

  local spray = self:addObject("spray", app.ParameterAdapter())
  spray:hardSet("Bias", 0.0)
  tie(op, "Spray", spray, "Out")
  self:addMonoBranch("spray", spray, "In", spray, "Out")

  local space = self:addObject("space", app.ParameterAdapter())
  space:hardSet("Bias", 0.4)
  tie(op, "Space", space, "Out")
  self:addMonoBranch("space", space, "In", space, "Out")

  local distance = self:addObject("distance", app.ParameterAdapter())
  distance:hardSet("Bias", 0.25)
  tie(op, "Distance", distance, "Out")
  self:addMonoBranch("distance", distance, "In", distance, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 0.4)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

function Sujet:onLoadViews()
  return {
    decay = GainBias {
      button = "dcy",
      description = "Decay — RT60 per-bin (0=short, 1=long)",
      branch = self.branches.decay,
      gainbias = self.objects.decay,
      range = self.objects.decay,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    damp = GainBias {
      button = "damp",
      description = "Damp — HF bin rolloff (tilt curve on RT60)",
      branch = self.branches.damp,
      gainbias = self.objects.damp,
      range = self.objects.damp,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.3
    },
    diffuse = GainBias {
      button = "diff",
      description = "Diffuse — per-bin phase randomization (0=coherent, 1=diffuse noise)",
      branch = self.branches.diffuse,
      gainbias = self.objects.diffuse,
      range = self.objects.diffuse,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.4
    },
    freeze = GainBias {
      button = "frz",
      description = "Freeze — blend decay toward infinite hold (g→1)",
      branch = self.branches.freeze,
      gainbias = self.objects.freeze,
      range = self.objects.freeze,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    smear = GainBias {
      button = "smr",
      description = "Smear — <0.5 Bloom swell / >0.5 Blur cloud (0.5 = off)",
      branch = self.branches.smear,
      gainbias = self.objects.smear,
      range = self.objects.smear,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    spray = GainBias {
      button = "spry",
      description = "Spray — noise-skirt magnitude injection (breathy halo; ≠ Diffuse)",
      branch = self.branches.spray,
      gainbias = self.objects.spray,
      range = self.objects.spray,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    space = GainBias {
      button = "spc",
      description = "Space — inter-channel decorrelation (width/envelopment; ≠ Diffuse noise)",
      branch = self.branches.space,
      gainbias = self.objects.space,
      range = self.objects.space,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.4
    },
    distance = GainBias {
      button = "dist",
      description = "Distance — push source into the space: ITDG + HF air-absorption + DRR (≠ Mix)",
      branch = self.branches.distance,
      gainbias = self.objects.distance,
      range = self.objects.distance,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.25
    },
    mix = GainBias {
      button = "mix",
      description = "Mix — dry/wet (Mix=1 → pure STFT wet, latency-aligned)",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = Encoder.getMap("unit"),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.4
    }
  }, {
    expanded = { "decay", "damp", "diffuse", "freeze", "smear", "spray", "space", "distance", "mix" },
    collapsed = {}
  }
end

return Sujet
