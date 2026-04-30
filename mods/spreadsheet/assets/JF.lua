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
  -- 8 framework sub-outs. The first two BOTH source from the C++ Mix
  -- outlet (Out1 = chain L, Out2 = chain R) so vanilla stereo chains
  -- receive MIX on both channels rather than MIX/1N. Sub-outs 3..8 are
  -- the per-voice taps, reachable on stolmine via the M6 picker cycle.
  args.channelCount = 8
  args.subOutLabels = {"mix", "mix R", "1N", "2N", "3N", "4N", "5N", "6N"}
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

  -- Wire 8 framework outlets. Out1 + Out2 both source from C++ Mix so
  -- vanilla stereo chains see MIX on both L and R. Phase 1 produces
  -- silence on each; Phases 2-4 fill in.
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
