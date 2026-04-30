local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local OptionControl = require "Unit.MenuControl.OptionControl"
local MenuHeader = require "Unit.MenuControl.Header"
local Encoder = require "Encoder"

-- JF — hex-voiced harmonically-coupled slope-engine voice. v1.
-- Phase 2: scalar single-voice slope engine on IDENTITY (1N).
-- Range + Mode in config menu (header hold) for now; promoted to
-- main view in Phase 4 (UI polish).
--
-- See planning/just-friends.md + planning/jf-initial-pass.md.

local JF = Class {}
JF:include(Unit)

function JF:init(args)
  args.title = "JF"
  args.mnemonic = "JF"
  args.channelCount = 8
  args.subOutLabels = {"mix", "mix R", "1N", "2N", "3N", "4N", "5N", "6N"}
  Unit.init(self, args)
end

function JF:onLoadGraph(channelCount)
  local jf = self:addObject("jf", libspreadsheet.JF())

  -- V/Oct CV. TIME knob (below) is the base rate/pitch per tech map;
  -- V/Oct adds octaves on top exponentially. Helicase/Plaits convention:
  -- buffer carries 0.1/octave so we apply 10x gain to get 1.0/octave
  -- into the C++ where powf(2, voctV) does the exponentiation.
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  local voctGain = self:addObject("voctGain", app.ConstantGain())
  voctGain:hardSet("Gain", 10.0)
  connect(tune, "Out", voctGain, "In")
  connect(voctGain, "Out", jf, "V/Oct")
  connect(tune, "Out", tuneRange, "In")
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  -- TIME knob (the slope-rate bias, separate from V/Oct CV per tech map).
  -- Uses a ParameterAdapter so the knob lives on the C++ TimeBias param.
  local time = self:addObject("time", app.ParameterAdapter())
  time:hardSet("Bias", 0.5)
  tie(jf, "TimeBias", time, "Out")
  self:addMonoBranch("time", time, "In", time, "Out")

  -- FM input (bipolar). Plumbed; DSP consumption arrives in Phase 4.
  local fm = self:addObject("fm", app.GainBias())
  local fmRange = self:addObject("fmRange", app.MinMax())
  connect(fm, "Out", jf, "FM In")
  connect(fm, "Out", fmRange, "In")
  self:addMonoBranch("fm", fm, "In", fm, "Out")

  -- IDENTITY trigger (1N gate input). Comparator-driven per the
  -- comparator-gate-threshold convention; the C++ side reads >0.5 as
  -- gate-high.
  local trig1N = self:addObject("trig1N", app.Comparator())
  trig1N:setGateMode()
  connect(trig1N, "Out", jf, "Trig 1N")
  self:addMonoBranch("trig1N", trig1N, "In", trig1N, "Out")

  -- Wire 8 framework outlets. Out1 + Out2 both source from C++ Mix so
  -- vanilla stereo chains see MIX on both L and R.
  connect(jf, "Mix",   self, "Out1") -- chain L (and mono primary)
  connect(jf, "Mix",   self, "Out2") -- chain R duplicate of MIX
  connect(jf, "Out1N", self, "Out3")
  connect(jf, "Out2N", self, "Out4")
  connect(jf, "Out3N", self, "Out5")
  connect(jf, "Out4N", self, "Out6")
  connect(jf, "Out5N", self, "Out7")
  connect(jf, "Out6N", self, "Out8")
end

local views = {
  expanded = { "tune", "time", "trig1N", "fm" },
  collapsed = {}
}

local timeMap = (function()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.0001)
  return m
end)()

function JF:onLoadViews(objects, branches)
  local controls = {}

  controls.tune = Pitch {
    button = "V/Oct",
    description = "V/Oct",
    branch = branches.tune,
    offset = objects.tune,
    range = objects.tuneRange
  }

  controls.time = GainBias {
    button = "time",
    description = "TIME",
    branch = branches.time,
    gainbias = objects.time,
    range = objects.time,
    biasMap = timeMap,
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.5
  }

  controls.trig1N = Gate {
    button = "1N",
    description = "Trigger 1N (IDENTITY)",
    branch = branches.trig1N,
    comparator = objects.trig1N
  }

  controls.fm = GainBias {
    button = "FM",
    description = "FM Input",
    branch = branches.fm,
    gainbias = objects.fm,
    range = objects.fmRange,
    biasMap = Encoder.getMap("[-1,1]"),
    biasUnits = app.unitNone,
    initialBias = 0.0
  }

  return controls, views
end

local menu = {
  "rangeHeader",
  "range",
  "modeHeader",
  "mode"
}

function JF:onShowMenu(objects, branches)
  local controls = {}

  controls.rangeHeader = MenuHeader {
    description = "Range:"
  }

  controls.range = OptionControl {
    description = "Range",
    option = objects.jf:getOption("Range"),
    choices = { "shape", "sound" }
  }

  controls.modeHeader = MenuHeader {
    description = "Mode:"
  }

  controls.mode = OptionControl {
    description = "Mode",
    option = objects.jf:getOption("Mode"),
    choices = { "trans", "sust", "cycle" }
  }

  return controls, menu
end

return JF
