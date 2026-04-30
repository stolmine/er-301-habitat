local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

-- JF — hex-voiced harmonically-coupled slope-engine voice. v1 phase 1
-- skeleton: declares the 7 sub-out topology, MIX-first ordering. No
-- DSP wired yet — unit produces silence. Validates the multi-output
-- declaration end-to-end before voice DSP comes online.
--
-- See planning/just-friends.md + planning/jf-initial-pass.md.

local JF = Class {}
JF:include(Unit)

function JF:init(args)
  args.title = "JF"
  args.mnemonic = "JF"
  -- 7 sub-outs. Sub-out 1 = MIX = primary (vanilla auto-wires here).
  -- M6 cycles in author-declared order: mix, then 1N..6N.
  args.channelCount = 7
  args.subOutLabels = {"mix", "1N", "2N", "3N", "4N", "5N", "6N"}
  Unit.init(self, args)
end

function JF:onLoadGraph(channelCount)
  local jf = self:addObject("jf", libspreadsheet.JF())

  -- V/Oct + tune offset (TIME ply). Phase 1 wires the param surface;
  -- DSP consumption added in Phase 2.
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  tune:hardSet("Offset", 0.0)
  connect(tune, "Out", jf, "V/Oct")
  connect(tune, "Out", tuneRange, "In")
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  -- FM input branch. Bipolar; Phase 4 adds AC-coupling switching.
  local fm = self:addObject("fm", app.GainBias())
  local fmRange = self:addObject("fmRange", app.MinMax())
  connect(fm, "Out", jf, "FM In")
  connect(fm, "Out", fmRange, "In")
  self:addMonoBranch("fm", fm, "In", fm, "Out")

  -- 7 sub-outs wired direct from the JF object to the unit boundary.
  -- Phase 1 produces silence on each; Phases 2-4 fill in.
  connect(jf, "Out1", self, "Out1") -- MIX
  connect(jf, "Out2", self, "Out2") -- 1N
  connect(jf, "Out3", self, "Out3") -- 2N
  connect(jf, "Out4", self, "Out4") -- 3N
  connect(jf, "Out5", self, "Out5") -- 4N
  connect(jf, "Out6", self, "Out6") -- 5N
  connect(jf, "Out7", self, "Out7") -- 6N
end

local views = {
  expanded = { "tune", "fm" },
  collapsed = {}
}

function JF:onLoadViews(objects, branches)
  local controls = {}

  controls.tune = Pitch {
    button = "V/Oct",
    description = "V/Oct",
    branch = branches.tune,
    offset = objects.tune,
    range = objects.tuneRange
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

return JF
