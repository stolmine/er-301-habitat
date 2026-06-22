local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Pitch = require "Unit.ViewControl.Pitch"
local CanalsOverviewControl = require "spreadsheet.CanalsOverviewControl"
local ModeSelector = require "spreadsheet.ModeSelector"
local Encoder = require "Encoder"
local MenuHeader = require "Unit.MenuControl.Header"
local OptionMenu = require "Unit.MenuControl.OptionControl"

local freqMap = (function()
  local map = app.LinearDialMap(-48, 48)
  map:setSteps(12, 1, 0.1, 0.01)
  return map
end)()

local outputMap = (function()
  local map = app.LinearDialMap(0, 3)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end)()

local outputNames = {
  [0] = "LOW",
  [1] = "CTR",
  [2] = "HIGH",
  [3] = "ALL"
}

local modeMap = (function()
  local map = app.LinearDialMap(0, 1)
  map:setSteps(1, 1, 1, 1)
  map:setRounding(1)
  return map
end)()

local modeNames = {
  [0] = "Xover",
  [1] = "Formnt"
}

local Canals = Class {}
Canals:include(Unit)

function Canals:init(args)
  args.title = "Canals"
  args.mnemonic = "Ca"
  -- 5 sub-outs:
  --   1: Out    — fader-selected mix (stereo L on stereo chains, mono on mono)
  --   2: Out R  — stereo R, silent on mono chains
  --   3: LOW    — parallel L-side tap (mono regardless of chain stereo)
  --   4: CENTRE — parallel L-side tap
  --   5: HIGH   — parallel L-side tap
  --
  -- Stereo via dual mono instances (Three Sisters hardware is mono;
  -- "proper stereo" = parallel placement in rack). Per-block parallel
  -- taps are derived from the L instance; if user needs R-side LOW
  -- separately, parallel-place a second Canals on an R-only chain.
  args.channelCount = 5
  args.subOutLabels = { "Out", "Out R", "LOW", "CENTRE", "HIGH" }
  Unit.init(self, args)
end

function Canals:onLoadGraph(channelCount)
  -- Single instance — Three Sisters is mono hardware. Both stereo
  -- chain channels see the same signal; for true stereo Canals,
  -- parallel-place two units.
  local op = self:addObject("op", libspreadsheet.Canals())
  connect(self, "In1", op, "In")

  -- Per-block input branches (LOW / CENTRE / HIGH). Each is a
  -- normalling jack: when nothing is patched, the block sources
  -- from the main In (ALL). Patching into one of these overrides
  -- ALL for that block.
  --
  -- Phase 2: GainBias objects + standard GainBias view (visible
  -- level fader, default unity gain / zero bias). Phase 3 will
  -- replace the view with a custom no-fader sub-button control on
  -- the consolidated overview ply. Phase 4 adds auto-polling of
  -- branch-chain unit count to drive the Patched flags.
  local lowIn = self:addObject("lowIn", app.GainBias())
  lowIn:hardSet("Gain", 1.0)   -- unity passthrough by default
  lowIn:hardSet("Bias", 0.0)
  local lowInRange = self:addObject("lowInRange", app.MinMax())
  connect(lowIn, "Out", lowInRange, "In")
  connect(lowIn, "Out", op, "Low In")
  self:addMonoBranch("lowIn", lowIn, "In", lowIn, "Out")

  local centreIn = self:addObject("centreIn", app.GainBias())
  centreIn:hardSet("Gain", 1.0)
  centreIn:hardSet("Bias", 0.0)
  local centreInRange = self:addObject("centreInRange", app.MinMax())
  connect(centreIn, "Out", centreInRange, "In")
  connect(centreIn, "Out", op, "Centre In")
  self:addMonoBranch("centreIn", centreIn, "In", centreIn, "Out")

  local highIn = self:addObject("highIn", app.GainBias())
  highIn:hardSet("Gain", 1.0)
  highIn:hardSet("Bias", 0.0)
  local highInRange = self:addObject("highInRange", app.MinMax())
  connect(highIn, "Out", highInRange, "In")
  connect(highIn, "Out", op, "High In")
  self:addMonoBranch("highIn", highIn, "In", highIn, "Out")

  -- V/Oct
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  -- Fundamental
  local fundamental = self:addObject("fundamental", app.ParameterAdapter())
  fundamental:hardSet("Bias", 0.0)
  tie(op, "Fundamental", fundamental, "Out")
  self:addMonoBranch("fundamental", fundamental, "In", fundamental, "Out")

  -- Span
  local span = self:addObject("span", app.ParameterAdapter())
  span:hardSet("Bias", 0.25)
  tie(op, "Span", span, "Out")
  self:addMonoBranch("span", span, "In", span, "Out")

  -- Quality
  local quality = self:addObject("quality", app.ParameterAdapter())
  quality:hardSet("Bias", 0.0)
  tie(op, "Quality", quality, "Out")
  self:addMonoBranch("quality", quality, "In", quality, "Out")

  -- Output fader (drives sub-out 1+2 morph content; sub-outs 3-5
  -- always carry direct per-block taps regardless of fader position).
  local output = self:addObject("output", app.ParameterAdapter())
  output:hardSet("Bias", 0.0)
  tie(op, "Output", output, "Out")
  self:addMonoBranch("output", output, "In", output, "Out")

  -- Mode
  local mode = self:addObject("mode", app.ParameterAdapter())
  mode:hardSet("Bias", 0)
  tie(op, "Mode", mode, "Out")
  self:addMonoBranch("mode", mode, "In", mode, "Out")

  -- Wire 5 framework outlets LAST — matches JF's pattern (output
  -- connects after all params/branches set up). Prior placement
  -- (connects immediately after addObject) appeared to silently
  -- break sub-out picker subscriptions for non-primary sub-outs
  -- (Out3-5) while leaving the scope viewer working. Reordering
  -- mirrors the only known-working multi-out unit in the package.
  connect(op, "Out",    self, "Out1") -- primary: fader morph
  connect(op, "Out",    self, "Out2") -- stereo R duplicate of primary
  connect(op, "Low",    self, "Out3")
  connect(op, "Centre", self, "Out4")
  connect(op, "High",   self, "Out5")
end

function Canals:onLoadViews()
  return {
    tune = Pitch {
      button      = "V/oct",
      branch      = self.branches.tune,
      description = "V/oct",
      offset      = self.objects.tune,
      range       = self.objects.tuneRange
    },
    fundamental = GainBias {
      button        = "freq",
      description   = "Fundamental",
      branch        = self.branches.fundamental,
      gainbias      = self.objects.fundamental,
      range         = self.objects.fundamental,
      biasMap       = freqMap,
      biasPrecision = 1,
      initialBias   = 0.0
    },
    span = GainBias {
      button        = "span",
      description   = "Span",
      branch        = self.branches.span,
      gainbias      = self.objects.span,
      range         = self.objects.span,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.25
    },
    quality = GainBias {
      button        = "qual",
      description   = "Quality",
      branch        = self.branches.quality,
      gainbias      = self.objects.quality,
      range         = self.objects.quality,
      biasMap       = Encoder.getMap("[-1,1]"),
      biasPrecision = 2,
      initialBias   = 0.0
    },
    output = ModeSelector {
      button        = "out",
      description   = "Output",
      branch        = self.branches.output,
      gainbias      = self.objects.output,
      range         = self.objects.output,
      biasMap       = outputMap,
      biasUnits     = app.unitNone,
      biasPrecision = 1,
      initialBias   = 0.0,
      modeNames     = outputNames
    },
    mode = ModeSelector {
      button        = "mode",
      description   = "Mode",
      branch        = self.branches.mode,
      gainbias      = self.objects.mode,
      range         = self.objects.mode,
      biasMap       = modeMap,
      biasUnits     = app.unitNone,
      biasPrecision = 0,
      initialBias   = 0,
      modeNames     = modeNames
    },
    -- Overview ply consolidates the three per-block input branches
    -- under one sub-display with MiniScope previews + sub-button
    -- dives. No level/bias visible — backend unity passthrough.
    overview = CanalsOverviewControl {
      button       = "in",
      lowBranch    = self.branches.lowIn,
      centreBranch = self.branches.centreIn,
      highBranch   = self.branches.highIn
    }
  }, {
    expanded  = { "overview", "mode", "tune", "fundamental", "span", "quality", "output" },
    collapsed = {}
  }
end

-- Menu options.
-- AllEnabled (menu-level toggle) is the user-facing global "ignore
-- main In" switch — kept out of the normal ply surface since it's
-- a rare-touch config decision.
--
-- LowPatched/CentrePatched/HighPatched are EXPOSED IN MENU FOR
-- VALIDATION ONLY in Phase 2. Phase 4 will replace these manual
-- toggles with Lua-side polling of the branch's chain unit count
-- so patched state is auto-detected, and the menu items will move
-- to the C++ side as pure routing flags (not user-toggleable). For
-- now they're here so the override routing can be exercised
-- without requiring the polling logic to land first.
function Canals:onShowMenu(objects, branches)
  return {
    routingHeader = MenuHeader { description = "Routing" },
    allEnabled = OptionMenu {
      description = "ALL Input",
      option = objects.op:getOption("AllEnabled"),
      choices = { "enabled", "disabled" }
    },
    lowPatched = OptionMenu {
      description = "Low Patched (test)",
      option = objects.op:getOption("LowPatched"),
      choices = { "off (use ALL)", "on (override)" }
    },
    centrePatched = OptionMenu {
      description = "Centre Patched (test)",
      option = objects.op:getOption("CentrePatched"),
      choices = { "off (use ALL)", "on (override)" }
    },
    highPatched = OptionMenu {
      description = "High Patched (test)",
      option = objects.op:getOption("HighPatched"),
      choices = { "off (use ALL)", "on (override)" }
    }
  }, { "routingHeader", "allEnabled", "lowPatched", "centrePatched", "highPatched" }
end

return Canals
