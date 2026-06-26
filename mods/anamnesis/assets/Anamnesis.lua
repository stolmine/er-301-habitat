-- Anamnesis -- spatial-glitch instrument (CM4-only). A short-buffer
-- micro-looper fused with a continuously-morphing spatial field via a
-- global CLOCK, cross-fed and Spiral-governed. Internal-stereo: one
-- object holds the shared coherent L/R field, so a mono source is
-- fanned to both inlets and spread by the field.
--
-- Phase 1.1 (0.1.0.1): the spatial-field STAGE 2 -- a unitary N=8
-- Householder FDN tail with per-line Jot T60 decay (size-independent)
-- + a 4-stage Schroeder input diffuser. Size / Decay / Diffusion / Mix.
-- Sparse taps, the alpha-morph, looper, CLOCK, and cross-feedback come
-- in later phases per planning/spatial-glitch-impl/99-build-order.md.

local app = app
local libanamnesis = require "anamnesis.libanamnesis"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local OptionControl = require "Unit.MenuControl.OptionControl"
local MenuHeader = require "Unit.MenuControl.Header"

local floatMap = function(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local zeroOneMap = floatMap(0, 1)

-- Bipolar Speed map: the value IS the rate multiplier, -2 .. +2x. Coarse
-- clicks land on 0.25 steps so 0.5x (half) / 1x / 2x (double) are detents;
-- 0 = stall, negative = reverse. (Tape: pitch; Stretch: time.)
local speedMap = app.LinearDialMap(-2, 2)
speedMap:setSteps(0.25, 0.05, 0.01, 0.001)

local Anamnesis = Class {}
Anamnesis:include(Unit)

function Anamnesis:init(args)
  args.title = "Anamnesis"
  args.mnemonic = "An"
  Unit.init(self, args)
end

function Anamnesis:onLoadGraph(channelCount)
  local op = self:addObject("op", libanamnesis.Anamnesis())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  else
    -- Mono fan-out: feed In1 to both channels so the field can spread a
    -- mono source. Out R is unused in mono.
    connect(self, "In1", op, "In R")
  end

  local length = self:addObject("length", app.ParameterAdapter())
  length:hardSet("Bias", 0.4)
  tie(op, "Length", length, "Out")
  self:addMonoBranch("length", length, "In", length, "Out")

  local speed = self:addObject("speed", app.ParameterAdapter())
  speed:hardSet("Bias", 1.0)
  tie(op, "Speed", speed, "Out")
  self:addMonoBranch("speed", speed, "In", speed, "Out")

  local freeze = self:addObject("freeze", app.Comparator())
  freeze:setToggleMode()
  connect(freeze, "Out", op, "Freeze")
  self:addMonoBranch("freeze", freeze, "In", freeze, "Out")

  local size = self:addObject("size", app.ParameterAdapter())
  size:hardSet("Bias", 0.5)
  tie(op, "Size", size, "Out")
  self:addMonoBranch("size", size, "In", size, "Out")

  local decay = self:addObject("decay", app.ParameterAdapter())
  decay:hardSet("Bias", 0.5)
  tie(op, "Decay", decay, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")

  local diffusion = self:addObject("diffusion", app.ParameterAdapter())
  diffusion:hardSet("Bias", 0.6)
  tie(op, "Diffusion", diffusion, "Out")
  self:addMonoBranch("diffusion", diffusion, "In", diffusion, "Out")

  local density = self:addObject("density", app.ParameterAdapter())
  density:hardSet("Bias", 0.5)
  tie(op, "Density", density, "Out")
  self:addMonoBranch("density", density, "In", density, "Out")

  local mod = self:addObject("mod", app.ParameterAdapter())
  mod:hardSet("Bias", 0.3)
  tie(op, "Mod", mod, "Out")
  self:addMonoBranch("mod", mod, "In", mod, "Out")

  local regen = self:addObject("regen", app.ParameterAdapter())
  regen:hardSet("Bias", 0.0)
  tie(op, "Regen", regen, "Out")
  self:addMonoBranch("regen", regen, "In", regen, "Out")

  local clock = self:addObject("clock", app.ParameterAdapter())
  clock:hardSet("Bias", 1.0)
  tie(op, "Clock", clock, "Out")
  self:addMonoBranch("clock", clock, "In", clock, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 0.4)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")

  local source = self:addObject("source", app.ParameterAdapter())
  source:hardSet("Bias", 1.0)
  tie(op, "Source", source, "Out")
  self:addMonoBranch("source", source, "In", source, "Out")

  local directloop = self:addObject("directloop", app.ParameterAdapter())
  directloop:hardSet("Bias", 0.0)
  tie(op, "DirectLoop", directloop, "Out")
  self:addMonoBranch("directloop", directloop, "In", directloop, "Out")

  local spread = self:addObject("spread", app.ParameterAdapter())
  spread:hardSet("Bias", 0.5)
  tie(op, "Spread", spread, "Out")
  self:addMonoBranch("spread", spread, "In", spread, "Out")
end

function Anamnesis:onLoadViews()
  return {
    length = GainBias {
      button = "len",
      description = "Length -- loop length (~20 ms .. 2 s)",
      branch = self.branches.length,
      gainbias = self.objects.length,
      range = self.objects.length,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.4
    },
    speed = GainBias {
      button = "spd",
      description = "Speed -- bipolar rate -2..2x (Tape:pitch / Stretch:time); 0=stall",
      branch = self.branches.speed,
      gainbias = self.objects.speed,
      range = self.objects.speed,
      biasMap = speedMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    freeze = Gate {
      button = "frz",
      description = "Freeze -- hold the loop (toggle / gate)",
      branch = self.branches.freeze,
      comparator = self.objects.freeze
    },
    size = GainBias {
      button = "size",
      description = "Size -- field extent / delay-line lengths (room->hall)",
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
      description = "Decay -- RT60 (size-independent, ~0.2..20 s)",
      branch = self.branches.decay,
      gainbias = self.objects.decay,
      range = self.objects.decay,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    diffusion = GainBias {
      button = "diff",
      description = "Diffusion -- input allpass smear",
      branch = self.branches.diffusion,
      gainbias = self.objects.diffusion,
      range = self.objects.diffusion,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.6
    },
    density = GainBias {
      button = "dens",
      description = "Density -- plexus: sparse combs/taps <-> dense FDN wash (alpha-morph)",
      branch = self.branches.density,
      gainbias = self.objects.density,
      range = self.objects.density,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    mod = GainBias {
      button = "mod",
      description = "Mod -- FDN delay modulation (de-metallic / lush chorus)",
      branch = self.branches.mod,
      gainbias = self.objects.mod,
      range = self.objects.mod,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.3
    },
    regen = GainBias {
      button = "rgn",
      description = "Regen -- cross-feedback: field tail re-loops into the looper (Spiral-governed)",
      branch = self.branches.regen,
      gainbias = self.objects.regen,
      range = self.objects.regen,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    clock = GainBias {
      button = "clk",
      description = "Clock -- internal rate; down = slower+lower+grittier wet (1=full)",
      branch = self.branches.clock,
      gainbias = self.objects.clock,
      range = self.objects.clock,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    mix = GainBias {
      button = "mix",
      description = "Mix -- dry/wet",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.4
    },
    source = GainBias {
      button = "src",
      description = "Source -- field reverberates input (0) .. loop (1)",
      branch = self.branches.source,
      gainbias = self.objects.source,
      range = self.objects.source,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    directloop = GainBias {
      button = "dir",
      description = "DirectLoop -- clean glitched loop blended to the output",
      branch = self.branches.directloop,
      gainbias = self.objects.directloop,
      range = self.objects.directloop,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    spread = GainBias {
      button = "sprd",
      description = "Spread -- stereo width (0 mono / 0.5 normal / 1 wide)",
      branch = self.branches.spread,
      gainbias = self.objects.spread,
      range = self.objects.spread,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    }
  }, {
    expanded = { "length", "speed", "freeze", "size", "decay", "diffusion", "density", "mod", "regen", "clock", "mix", "source", "directloop", "spread" },
    collapsed = {}
  }
end

function Anamnesis:onShowMenu(objects, branches)
  return {
    looperHeader = MenuHeader { description = "Looper" },
    mode = OptionControl {
      description = "Mode",
      option = objects.op:getOption("Mode"),
      choices = { "Tape", "Stretch", "Env" }
    },
    sense = OptionControl {
      description = "Env sensitivity",
      option = objects.op:getOption("Sense"),
      choices = { "Low", "Med", "High" }
    },
    clockHeader = MenuHeader { description = "Clock" },
    clockmode = OptionControl {
      description = "Clock mode",
      option = objects.op:getOption("ClockMode"),
      choices = { "Steps", "Smooth" }
    },
    grit = OptionControl {
      description = "Grit",
      option = objects.op:getOption("Grit"),
      choices = { "Clean", "Normal", "Broken" }
    }
  }, { "looperHeader", "mode", "sense", "clockHeader", "clockmode", "grit" }
end

return Anamnesis
