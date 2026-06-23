-- Fabula -- the believable-room substrate of the Zaum package. A
-- Dattorro/Gardner figure-8 allpass tank with decorrelated Brownian
-- delay-line modulation: the smooth, lush, long-decay algorithmic
-- room the catalog lacks. Internal-stereo (cross-feedback inside the
-- tank lives in the C++ atom), so the Lua wiring just maps In1/In2 ->
-- In L/In R and Out L/Out R -> Out1/Out2.
--
-- 8 parameters surfaced via standard ParameterAdapter + GainBias plies.
-- Design doc: planning/fabula-design.md. Roadmap: planning/zaum-roadmap.md.
--
-- Sub-phase 0.1.0.1: the APFTank atom is a passthrough stub; the unit
-- loads, installs, and appears in the browser with all 8 controls
-- present but inert. DSP lands across 0.1.0.2 .. 0.1.0.6.

local app = app
local libzaum = require "zaum.libzaum"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"

local floatMap = function(min, max, precision)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local zeroOneMap = floatMap(0, 1)

local Fabula = Class {}
Fabula:include(Unit)

function Fabula:init(args)
  args.title = "Fabula"
  args.mnemonic = "Fa"
  Unit.init(self, args)
end

function Fabula:onLoadGraph(channelCount)
  local op = self:addObject("op", libzaum.APFTank())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  -- 8 ParameterAdapter ties for the user-facing parameters.
  local size = self:addObject("size", app.ParameterAdapter())
  size:hardSet("Bias", 0.5)
  tie(op, "Size", size, "Out")
  self:addMonoBranch("size", size, "In", size, "Out")

  local decay = self:addObject("decay", app.ParameterAdapter())
  decay:hardSet("Bias", 0.5)
  tie(op, "Decay", decay, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")

  local damp = self:addObject("damp", app.ParameterAdapter())
  damp:hardSet("Bias", 0.25)
  tie(op, "Damp", damp, "Out")
  self:addMonoBranch("damp", damp, "In", damp, "Out")

  local diffusion = self:addObject("diffusion", app.ParameterAdapter())
  diffusion:hardSet("Bias", 0.6)
  tie(op, "Diffusion", diffusion, "Out")
  self:addMonoBranch("diffusion", diffusion, "In", diffusion, "Out")

  local mod = self:addObject("mod", app.ParameterAdapter())
  mod:hardSet("Bias", 0.3)
  tie(op, "Mod", mod, "Out")
  self:addMonoBranch("mod", mod, "In", mod, "Out")

  local modRate = self:addObject("modRate", app.ParameterAdapter())
  modRate:hardSet("Bias", 0.2)
  tie(op, "ModRate", modRate, "Out")
  self:addMonoBranch("modRate", modRate, "In", modRate, "Out")

  local predelay = self:addObject("predelay", app.ParameterAdapter())
  predelay:hardSet("Bias", 0.0)
  tie(op, "Predelay", predelay, "Out")
  self:addMonoBranch("predelay", predelay, "In", predelay, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 0.5)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

function Fabula:onLoadViews()
  return {
    size = GainBias {
      button = "size",
      description = "Size (delay scale)",
      branch = self.branches.size,
      gainbias = self.objects.size,
      range = self.objects.size,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    decay = GainBias {
      button = "dcy",
      description = "Decay (RT60)",
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
      description = "Damp (HF rolloff)",
      branch = self.branches.damp,
      gainbias = self.objects.damp,
      range = self.objects.damp,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.25
    },
    diffusion = GainBias {
      button = "diff",
      description = "Diffusion",
      branch = self.branches.diffusion,
      gainbias = self.objects.diffusion,
      range = self.objects.diffusion,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.6
    },
    mod = GainBias {
      button = "mod",
      description = "Modulation Depth",
      branch = self.branches.mod,
      gainbias = self.objects.mod,
      range = self.objects.mod,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.3
    },
    modRate = GainBias {
      button = "rate",
      description = "Modulation Rate",
      branch = self.branches.modRate,
      gainbias = self.objects.modRate,
      range = self.objects.modRate,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.2
    },
    predelay = GainBias {
      button = "pre",
      description = "Predelay",
      branch = self.branches.predelay,
      gainbias = self.objects.predelay,
      range = self.objects.predelay,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    mix = GainBias {
      button = "mix",
      description = "Dry/Wet",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    }
  }, {
    expanded = { "size", "decay", "damp", "diffusion", "mod", "modRate", "predelay", "mix" },
    collapsed = {}
  }
end

return Fabula
