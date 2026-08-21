-- Channel Strip -- six sections, each with a headline parameter on the
-- encoder, its full board on ENTER, and a true bypass on SHIFT.
--
-- FIVE SECTIONS WIRED: Dynamics (Pop3Dynamics), Filter (two
-- ParametricBand in replacing mode), EQ (three more), Drive
-- (DriveStage) and Out. Punch is specified in
-- planning/strata-channel-strip.md and is NOT wired yet -- dead plies would be worse than absent ones, so
-- they appear when their DSP does, taking the strip to its planned
-- seven plies.
--
-- The Filter section deliberately does NOT use Capacitor2, which the
-- design note originally named. Measured on real A8 codegen,
-- Capacitor2Mono is 356 instructions with 223 DOUBLE-precision ops and
-- that is MONO -- roughly 712 stereo, against 30 for a ParametricBand
-- stereo pass. It is a fine character filter and a poor utility filter;
-- character belongs in the Drive section.
--
-- LAYOUT, and why. SECTION_PLY is 42 px against a 256 px display, so
-- exactly SIX PLIES ARE VISIBLE. The planned full set is
-- Overview / Dynamics / Filter / EQ / Drive / Punch / Out = 7, so one
-- scrolls, and in signal-flow order that is Out -- whose Level is the
-- most-reached-for control on any strip. Hence the OVERVIEW ply's
-- encoder drives output level: the overview is the master ply, master
-- level belongs there, and the most-used control stays visible without
-- reordering the sections.
--
-- Design: planning/strata-channel-strip.md, "UI: SETTLED".
-- DSP: atoms/ChannelStrip.h over atoms/Pop3Dynamics.h + ParametricBand.h

local app = app
local libhouse = require "house.libhouse"
local Encoder = require "Encoder"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Fader = require "Unit.ViewControl.Fader"
local SectionGate = require "house.SectionGate"

local function linMap(min, max, coarse, fine)
  local m = app.LinearDialMap(min, max)
  m:setSteps(coarse, fine, fine / 10, fine / 100)
  return m
end

local ChannelStrip = Class {}
ChannelStrip:include(Unit)

function ChannelStrip:init(args)
  args.title = "Channel Strip"
  args.mnemonic = "CS"
  Unit.init(self, args)
end

function ChannelStrip:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.ChannelStrip())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  else
    -- Mono: feed the right channel so its section state stays matched,
    -- and ignore its output. An unfed inlet reads whatever the previous
    -- unit left behind.
    connect(self, "In1", op, "In R")
  end

  local function adapter(name, param, bias)
    local o = self:addObject(name, app.ParameterAdapter())
    o:hardSet("Gain", 0.0)   -- CV is opt-in catalog-wide
    o:hardSet("Bias", bias)
    tie(op, param, o, "Out")
    self:addMonoBranch(name, o, "In", o, "Out")
    return o
  end

  adapter("dynAmount", "Compress", 0.0)
  adapter("dynThresh", "Comp Thresh", 0.5)
  adapter("dynAttack", "Comp Attack", 0.3)
  adapter("dynRelease", "Comp Release", 0.5)
  adapter("gateThresh", "Gate Thresh", 0.0)
  adapter("gateAmount", "Gate Amount", 0.0)

  adapter("hpFreq", "HP Freq", 10.0)
  adapter("lpFreq", "LP Freq", 20000.0)

  adapter("eqLow", "EQ Low", 0.0)
  adapter("eqMidFreq", "EQ Mid Freq", 1000.0)
  adapter("eqMid", "EQ Mid", 0.0)
  adapter("eqMidQ", "EQ Mid Q", 1.0)
  adapter("eqHigh", "EQ High", 0.0)

  adapter("drive", "Drive", 0.0)
  adapter("slew", "Slew", 0.0)

  adapter("level", "Level", 1.0)
end

function ChannelStrip:onLoadViews()
  local op = self.objects.op

  -- Expansion members use Fader, not GainBias: GainBias REQUIRES a
  -- branch, so every sub-parameter would add a patchable CV inlet. At
  -- roughly a dozen sub-params that is a dozen inlets nobody asked for.
  local function fd(key, description, map, prec)
    return Fader {
      button = key,
      description = description,
      param = self.objects[key]:getParameter("Bias"),
      map = map,
      units = app.unitNone,
      precision = prec or 2
    }
  end

  local dbMap = linMap(-15, 15, 3, 0.5)
  local unitMap = Encoder.getMap("[0,1]")

  return {
    -- The master ply. Its encoder is output level, so the most-used
    -- control is on the always-visible ply even though Out itself
    -- scrolls off once all six sections exist.
    master = GainBias {
      button = "level",
      description = "Level",
      branch = self.branches.level,
      gainbias = self.objects.level,
      range = self.objects.level,
      biasMap = linMap(0, 4, 0.5, 0.05),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },

    dyn = SectionGate {
      button = "comp",
      description = "Compress",
      sectionName = "dyn",
      engageOption = op:getOption("Dyn On"),
      branch = self.branches.dynAmount,
      gainbias = self.objects.dynAmount,
      range = self.objects.dynAmount,
      biasMap = unitMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    flt = SectionGate {
      button = "hp",
      description = "HP Freq",
      sectionName = "flt",
      engageOption = op:getOption("Flt On"),
      branch = self.branches.hpFreq,
      gainbias = self.objects.hpFreq,
      range = self.objects.hpFreq,
      biasMap = linMap(10, 2000, 100, 10),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 10.0
    },
    eq = SectionGate {
      button = "eq",
      description = "EQ Mid",
      sectionName = "eq",
      engageOption = op:getOption("EQ On"),
      branch = self.branches.eqMid,
      gainbias = self.objects.eqMid,
      range = self.objects.eqMid,
      biasMap = dbMap,
      biasUnits = app.unitNone,
      biasPrecision = 1,
      initialBias = 0.0
    },
    drv = SectionGate {
      button = "drive",
      description = "Drive",
      sectionName = "drv",
      engageOption = op:getOption("Drv On"),
      branch = self.branches.drive,
      gainbias = self.objects.drive,
      range = self.objects.drive,
      biasMap = unitMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    out = SectionGate {
      button = "out",
      description = "Level",
      sectionName = "out",
      engageOption = op:getOption("Out On"),
      branch = self.branches.level,
      gainbias = self.objects.level,
      range = self.objects.level,
      biasMap = linMap(0, 4, 0.5, 0.05),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },

    dynThreshF = fd("dynThresh", "Threshold", unitMap),
    dynAttackF = fd("dynAttack", "Attack", unitMap),
    dynReleaseF = fd("dynRelease", "Release", unitMap),
    gateThreshF = fd("gateThresh", "Gate Thr", unitMap),
    gateAmountF = fd("gateAmount", "Gate Amt", unitMap),

    lpFreqF = fd("lpFreq", "LP Freq", linMap(1000, 20000, 2000, 200), 0),

    eqLowF = fd("eqLow", "Low", dbMap, 1),
    eqMidFreqF = fd("eqMidFreq", "Mid Freq", linMap(120, 8000, 500, 50), 0),
    eqMidQF = fd("eqMidQ", "Mid Q", linMap(0.3, 10, 1, 0.1)),
    eqHighF = fd("eqHigh", "High", dbMap, 1),

    slewF = fd("slew", "Slew", unitMap)
  }, {
    expanded = { "master", "dyn", "flt", "eq", "drv", "out" },
    collapsed = {},
    -- The original control key leads each expansion, or the custom
    -- graphic is replaced by a plain fader on expansion.
    dyn = { "dyn", "dynThreshF", "dynAttackF", "dynReleaseF", "gateThreshF", "gateAmountF" },
    flt = { "flt", "lpFreqF" },
    drv = { "drv", "slewF" },
    eq = { "eq", "eqLowF", "eqMidFreqF", "eqMidQF", "eqHighF" }
  }
end

return ChannelStrip
