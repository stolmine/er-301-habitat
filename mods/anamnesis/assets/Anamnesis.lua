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
local Encoder = require "Encoder"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local OptionControl = require "Unit.MenuControl.OptionControl"
local MenuHeader = require "Unit.MenuControl.Header"
local AnamSubControl = require "anamnesis.AnamSubControl"
local AnamFieldControl = require "anamnesis.AnamFieldControl"
local AnamFieldGate = require "anamnesis.AnamFieldGate"

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
    -- Looper ply: no single main -- Speed (bipolar -2..2x: pitch/time, reverse,
    -- stall) and Length (loop window) are BOTH shown as subs by default
    -- (defaultParamMode); tap spd/len to pick which the encoder edits. The field
    -- is the main visual. Speed keeps the control's CV branch.
    looper = AnamFieldControl {
      button = "loop",
      description = "Loop",
      branch = self.branches.speed,
      gainbias = self.objects.speed,
      range = self.objects.speed,
      biasMap = speedMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0,
      op = self.objects.op,
      canvasIndex = 0,
      canvasCount = 6,
      feature = 1, -- kLooper: raindrop ripples
      defaultParamMode = true,
      subs = {
        { param = self.objects.speed:getParameter("Bias"),  button = "spd", col = 1, map = speedMap,  precision = 2 },
        { param = self.objects.length:getParameter("Bias"), button = "len", col = 3, map = zeroOneMap, precision = 2 }
      }
    },
    freeze = AnamFieldGate {
      button = "frz",
      description = "Freeze",
      branch = self.branches.freeze,
      comparator = self.objects.freeze,
      op = self.objects.op,
      canvasIndex = 1,
      canvasCount = 6
    },
    field = AnamFieldControl {
      button = "size",
      description = "Size",
      branch = self.branches.size,
      gainbias = self.objects.size,
      range = self.objects.size,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5,
      op = self.objects.op,
      canvasIndex = 2,
      canvasCount = 6,
      subs = {
        { param = self.objects.decay:getParameter("Bias"), button = "dcy", map = zeroOneMap, precision = 2 },
        { param = self.objects.mod:getParameter("Bias"),   button = "mod", map = zeroOneMap, precision = 2 },
        { param = self.objects.regen:getParameter("Bias"), button = "rgn", map = zeroOneMap, precision = 2 }
      }
    },
    density = AnamFieldControl {
      button = "dens",
      description = "Density",
      branch = self.branches.density,
      gainbias = self.objects.density,
      range = self.objects.density,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5,
      op = self.objects.op,
      canvasIndex = 3,
      canvasCount = 6,
      subs = {
        { param = self.objects.diffusion:getParameter("Bias"), button = "diff", map = zeroOneMap, precision = 2 }
      }
    },
    overview = AnamFieldControl {
      button = "clk",
      description = "Clock",
      branch = self.branches.clock,
      gainbias = self.objects.clock,
      range = self.objects.clock,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0,
      op = self.objects.op,
      canvasIndex = 4,
      canvasCount = 6,
      subs = {
        { param = self.objects.source:getParameter("Bias"),     button = "src",  map = zeroOneMap, precision = 2 },
        { param = self.objects.directloop:getParameter("Bias"), button = "dir",  map = zeroOneMap, precision = 2 },
        { param = self.objects.spread:getParameter("Bias"),     button = "sprd", map = zeroOneMap, precision = 2 }
      }
    },
    mix = AnamFieldControl {
      button = "mix",
      description = "Mix",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = Encoder.getMap("unit"),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.4,
      op = self.objects.op,
      canvasIndex = 5,
      canvasCount = 6
    },
  }, {
    expanded = { "looper", "freeze", "field", "density", "overview", "mix" },
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
