-- Fabula -- the believable-room substrate of the Zaum package. A
-- Dattorro/Gardner figure-8 allpass tank with decorrelated Brownian
-- delay-line modulation: the smooth, lush, long-decay algorithmic
-- room the catalog lacks. Internal-stereo (cross-feedback inside the
-- tank lives in the C++ atom), so the Lua wiring just maps In1/In2 ->
-- In L/In R and Out L/Out R -> Out1/Out2.
--
-- 9 parameters surfaced via standard ParameterAdapter + GainBias plies.
-- Design doc: planning/fabula-design.md. Roadmap: planning/zaum-roadmap.md.
--
-- Sub-phase 0.1.0.9 (Tier 3): adds the Early parameter + discrete ER network.
-- Early=0 → pure diffuse hall (0.1.0.8 output unchanged).
-- Early up → discrete room reflections in the 7–70 ms window appear,
-- creating a more present, immediate room character.

local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Encoder = require "Encoder"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local FabulaOverviewControl = require "spreadsheet.FabulaOverviewControl"
local TransformGateControl = require "spreadsheet.TransformGateControl"
local MixHpfControl = require "spreadsheet.MixHpfControl"

local floatMap = function(min, max, precision)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local zeroOneMap = floatMap(0, 1)

-- Xform target selector (which param(s) a re-roll touches). Order matches
-- APFTank::applyRandomize: 0 = all-but-freeze (default), 1 = all, 2..8 = one
-- param, 9 = reset to defaults.
local xformTargetNames = {
  [0] = "noFrz", "all", "size", "dcay", "damp", "diff", "ER", "pre", "frz", "reset"
}

local function intMap(lo, hi)
  local map = app.LinearDialMap(lo, hi)
  map:setSteps(1, 1, 1, 1)
  map:setRounding(1)
  return map
end

local Fabula = Class {}
Fabula:include(Unit)

function Fabula:init(args)
  args.title = "Fabula"
  args.mnemonic = "Fa"
  Unit.init(self, args)
end

function Fabula:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.APFTank())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  -- 8 ParameterAdapter ties for the user-facing parameters.
  local size = self:addObject("size", app.ParameterAdapter())
  size:hardSet("Bias", 0.35)
  tie(op, "Size", size, "Out")
  self:addMonoBranch("size", size, "In", size, "Out")

  local decay = self:addObject("decay", app.ParameterAdapter())
  decay:hardSet("Bias", 0.55)
  tie(op, "Decay", decay, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")

  local damp = self:addObject("damp", app.ParameterAdapter())
  damp:hardSet("Bias", 0.25)
  tie(op, "Damp", damp, "Out")
  self:addMonoBranch("damp", damp, "In", damp, "Out")

  local diffusion = self:addObject("diffusion", app.ParameterAdapter())
  diffusion:hardSet("Bias", 0.45)
  tie(op, "Diffusion", diffusion, "Out")
  self:addMonoBranch("diffusion", diffusion, "In", diffusion, "Out")

  -- Mod / ModRate demoted to baked-in character (not exposed); the C++ op no
  -- longer registers those parameters, so there is nothing to tie here.

  local predelay = self:addObject("predelay", app.ParameterAdapter())
  predelay:hardSet("Bias", 0.041)
  tie(op, "Predelay", predelay, "Out")
  self:addMonoBranch("predelay", predelay, "In", predelay, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 0.40)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")

  -- Wet highpass corner (Hz), a sub under Mix. NOT an xform target (never added
  -- to setTopLevelBias), so a re-roll leaves the body/HPF alone.
  local hpf = self:addObject("hpf", app.ParameterAdapter())
  hpf:hardSet("Bias", 60)
  tie(op, "HPF", hpf, "Out")
  self:addMonoBranch("hpf", hpf, "In", hpf, "Out")

  local early = self:addObject("early", app.ParameterAdapter())
  early:hardSet("Bias", 0.4)
  tie(op, "Early", early, "Out")
  self:addMonoBranch("early", early, "In", early, "Out")

  local freeze = self:addObject("freeze", app.ParameterAdapter())
  freeze:hardSet("Bias", 0.0)
  tie(op, "Freeze", freeze, "Out")
  self:addMonoBranch("freeze", freeze, "In", freeze, "Out")

  -- Xform: a trigger/gate that re-rolls a new room (Pecto-style DESTRUCTIVE
  -- randomize - the op hardSets the adapter Bias params, so the knobs visibly
  -- move and the new room serializes). Target picks which param(s); Depth the
  -- blend from current toward random. Fire manually from the control's sub.
  local xformGate = self:addObject("xform", app.Comparator())
  xformGate:setTriggerMode()
  connect(xformGate, "Out", op, "Xform")
  self:addMonoBranch("xform", xformGate, "In", xformGate, "Out")

  local xformTarget = self:addObject("xformTarget", app.ParameterAdapter())
  xformTarget:hardSet("Bias", 0)
  tie(op, "Xform Target", xformTarget, "Out")
  self:addMonoBranch("xformTarget", xformTarget, "In", xformTarget, "Out")

  local xformDepth = self:addObject("xformDepth", app.ParameterAdapter())
  xformDepth:hardSet("Bias", 0.5)
  tie(op, "Xform Depth", xformDepth, "Out")
  self:addMonoBranch("xformDepth", xformDepth, "In", xformDepth, "Out")

  -- Hand the op pointers to the Bias params a re-roll touches. The 0..6 order
  -- matches APFTank::setTopLevelBias (Size/Decay/Damp/Diffusion/Early/Pre/Freeze).
  op:setTopLevelBias(0, size:getParameter("Bias"))
  op:setTopLevelBias(1, decay:getParameter("Bias"))
  op:setTopLevelBias(2, damp:getParameter("Bias"))
  op:setTopLevelBias(3, diffusion:getParameter("Bias"))
  op:setTopLevelBias(4, early:getParameter("Bias"))
  op:setTopLevelBias(5, predelay:getParameter("Bias"))
  op:setTopLevelBias(6, freeze:getParameter("Bias"))
end

-- Manual fire from the TransformGateControl sub (paramMode -> sub 3).
function Fabula:fireTransform()
  self.objects.op:fireRandomize()
end

function Fabula:onLoadViews()
  return {
    -- Overview: Size is the main dial; the fabric waterfall viz replaces the
    -- fader; tap-shift reveals Decay / Damp / Diffusion.
    size = FabulaOverviewControl {
      button = "size",
      description = "Size",
      branch = self.branches.size,
      gainbias = self.objects.size,
      range = self.objects.size,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.35,
      tank = self.objects.op,
      decayParam = self.objects.decay:getParameter("Bias"),
      dampParam = self.objects.damp:getParameter("Bias"),
      diffusionParam = self.objects.diffusion:getParameter("Bias")
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
      initialBias = 0.041
    },
    mix = MixHpfControl {
      button = "mix",
      description = "Dry/Wet",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = Encoder.getMap("unit"),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.40,
      hpfParam = self.objects.hpf:getParameter("Bias")
    },
    early = GainBias {
      button = "ER",
      description = "Early Reflections",
      branch = self.branches.early,
      gainbias = self.objects.early,
      range = self.objects.early,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.4
    },
    freeze = GainBias {
      button = "frz",
      description = "Freeze (living hold)",
      branch = self.branches.freeze,
      gainbias = self.objects.freeze,
      range = self.objects.freeze,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    xform = TransformGateControl {
      seq = self,
      button = "xform",
      description = "Randomize",
      branch = self.branches.xform,
      comparator = self.objects.xform,
      funcNames = xformTargetNames,
      funcMap = intMap(0, 9),
      funcParam = self.objects.xformTarget:getParameter("Bias"),
      paramALabel = "depth",
      factorParam = self.objects.xformDepth:getParameter("Bias"),
      factorMap = floatMap(0, 1),
      factorPrecision = 2
    },
    -- Expansion controls: full faders reached by pressing enter on their parent
    -- (Size -> Decay/Damp/Diff, Mix -> HPF). They bind the SAME adapter Bias as
    -- the parent's compact submenu readouts, so both stay in sync. Not listed in
    -- expanded/collapsed - they live only in the per-control views below (the
    -- impasto/parfait BandControl pattern).
    decay = GainBias {
      button = "dcy",
      description = "Decay",
      branch = self.branches.decay,
      gainbias = self.objects.decay,
      range = self.objects.decay,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.55
    },
    damp = GainBias {
      button = "damp",
      description = "Damp",
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
      initialBias = 0.45
    },
    hpf = GainBias {
      button = "hpf",
      description = "Wet Highpass",
      branch = self.branches.hpf,
      gainbias = self.objects.hpf,
      range = self.objects.hpf,
      biasMap = (function()
        local m = app.LinearDialMap(20, 500)
        m:setSteps(50, 10, 1, 1)
        return m
      end)(),
      biasUnits = app.unitHertz,
      biasPrecision = 0,
      initialBias = 60
    }
  }, {
    expanded = { "size", "predelay", "early", "freeze", "xform", "mix" },
    collapsed = {},
    -- Per-control expansion views (enter on the parent toggles into these).
    size = { "size", "decay", "damp", "diffusion" },
    mix = { "mix", "hpf" }
  }
end

return Fabula
