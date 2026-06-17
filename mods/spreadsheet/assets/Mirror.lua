local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

-- Maps
local f0Map = (function()
  local m = app.LinearDialMap(0.1, 2000)
  m:setSteps(100, 10, 1, 0.1)
  return m
end)()

local fineMap = (function()
  local m = app.LinearDialMap(-100, 100)
  m:setSteps(10, 1, 0.1, 0.01)
  return m
end)()

local zeroToOneMap = (function()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end)()

local Mirror = Class {}
Mirror:include(Unit)

function Mirror:init(args)
  args.title = "Mirror"
  args.mnemonic = "Mr"
  -- Mono phase 2: 5 sub-outs.
  --   1: Out   -- main output
  --   2: Clean -- bandlimited carrier reference (pre-Mirror)
  --   3: Fold  -- alias residual (Mirror output minus Clean)
  --   4: Sync  -- gate firing on internal sync edges (mod phase wrap)
  --   5: Mod   -- raw internal modulator sine
  args.channelCount = 5
  args.subOutLabels = { "Out", "Clean", "Fold", "Sync", "Mod" }
  Unit.init(self, args)
end

function Mirror:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Mirror())

  -- Sink chain input (this is a generator unit; the main input is
  -- ignored on the audio path but the framework expects a sink).
  local sink = self:addObject("sink", app.ConstantGain())
  sink:hardSet("Gain", 0.0)
  connect(self, "In1", sink, "In")

  -- V/Oct (10x scaling, Plaits / Helicase pattern).
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  local voctGain = self:addObject("voctGain", app.ConstantGain())
  voctGain:hardSet("Gain", 10.0)
  connect(tune, "Out", voctGain, "In")
  connect(voctGain, "Out", op, "V/Oct")
  connect(tune, "Out", tuneRange, "In")
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  -- FM input (audio-rate).
  local fm = self:addObject("fm", app.GainBias())
  local fmRange = self:addObject("fmRange", app.MinMax())
  connect(fm, "Out", op, "FM")
  connect(fm, "Out", fmRange, "In")
  self:addMonoBranch("fm", fm, "In", fm, "Out")

  -- Fundamental (base pitch).
  local f0 = self:addObject("f0", app.ParameterAdapter())
  f0:hardSet("Bias", 110.0)
  tie(op, "Fundamental", f0, "Out")
  self:addMonoBranch("f0", f0, "In", f0, "Out")

  -- Fine (cents offset).
  local fine = self:addObject("fine", app.ParameterAdapter())
  fine:hardSet("Bias", 0.0)
  tie(op, "Fine", fine, "Out")
  self:addMonoBranch("fine", fine, "In", fine, "Out")

  -- Source morph (sine -> poly3 -> self-FM).
  local source = self:addObject("source", app.ParameterAdapter())
  source:hardSet("Bias", 0.0)
  tie(op, "Source", source, "Out")
  self:addMonoBranch("source", source, "In", source, "Out")

  -- Push (drive into shaper).
  local push = self:addObject("push", app.ParameterAdapter())
  push:hardSet("Bias", 0.3)
  tie(op, "Push", push, "Out")
  self:addMonoBranch("push", push, "In", push, "Out")

  -- Mod Depth (FM index from internal modulator into carrier).
  local modDepth = self:addObject("modDepth", app.ParameterAdapter())
  modDepth:hardSet("Bias", 0.5)
  tie(op, "ModDepth", modDepth, "Out")
  self:addMonoBranch("modDepth", modDepth, "In", modDepth, "Out")

  -- Sync Threshold (cubic-around-locks: carrier/mod lock ratio).
  local syncThreshold = self:addObject("syncThreshold", app.ParameterAdapter())
  syncThreshold:hardSet("Bias", 0.0)
  tie(op, "SyncThreshold", syncThreshold, "Out")
  self:addMonoBranch("syncThreshold", syncThreshold, "In", syncThreshold, "Out")

  -- Mirror (internal Nyquist position).
  local mirror = self:addObject("mirror", app.ParameterAdapter())
  mirror:hardSet("Bias", 0.0)
  tie(op, "Mirror", mirror, "Out")
  self:addMonoBranch("mirror", mirror, "In", mirror, "Out")

  -- Level (output gain).
  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Bias", 0.5)
  tie(op, "Level", level, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")

  -- Wire the 5 framework outlets LAST (per Canals/JF pattern, sub-out
  -- subscriptions break silently if connects happen before all
  -- params/branches are set up).
  connect(op, "Out",   self, "Out1")
  connect(op, "Clean", self, "Out2")
  connect(op, "Fold",  self, "Out3")
  connect(op, "Sync",  self, "Out4")
  connect(op, "Mod",   self, "Out5")
end

function Mirror:onLoadViews()
  return {
    tune = Pitch {
      button      = "V/oct",
      branch      = self.branches.tune,
      description = "V/oct",
      offset      = self.objects.tune,
      range       = self.objects.tuneRange
    },
    f0 = GainBias {
      button        = "freq",
      description   = "Fundamental",
      branch        = self.branches.f0,
      gainbias      = self.objects.f0,
      range         = self.objects.f0,
      biasMap       = f0Map,
      biasUnits     = app.unitHertz,
      biasPrecision = 1,
      initialBias   = 110.0
    },
    fine = GainBias {
      button        = "fine",
      description   = "Fine",
      branch        = self.branches.fine,
      gainbias      = self.objects.fine,
      range         = self.objects.fine,
      biasMap       = fineMap,
      biasUnits     = app.unitCents,
      biasPrecision = 1,
      initialBias   = 0.0
    },
    source = GainBias {
      button        = "src",
      description   = "Source",
      branch        = self.branches.source,
      gainbias      = self.objects.source,
      range         = self.objects.source,
      biasMap       = zeroToOneMap,
      biasPrecision = 2,
      initialBias   = 0.0
    },
    push = GainBias {
      button        = "push",
      description   = "Push",
      branch        = self.branches.push,
      gainbias      = self.objects.push,
      range         = self.objects.push,
      biasMap       = zeroToOneMap,
      biasPrecision = 2,
      initialBias   = 0.3
    },
    modDepth = GainBias {
      button        = "mod",
      description   = "Mod Depth",
      branch        = self.branches.modDepth,
      gainbias      = self.objects.modDepth,
      range         = self.objects.modDepth,
      biasMap       = zeroToOneMap,
      biasPrecision = 2,
      initialBias   = 0.5
    },
    syncThreshold = GainBias {
      button        = "sync",
      description   = "Sync",
      branch        = self.branches.syncThreshold,
      gainbias      = self.objects.syncThreshold,
      range         = self.objects.syncThreshold,
      biasMap       = zeroToOneMap,
      biasPrecision = 2,
      initialBias   = 0.0
    },
    mirror = GainBias {
      button        = "mirr",
      description   = "Mirror",
      branch        = self.branches.mirror,
      gainbias      = self.objects.mirror,
      range         = self.objects.mirror,
      biasMap       = zeroToOneMap,
      biasPrecision = 2,
      initialBias   = 0.0
    },
    level = GainBias {
      button        = "lvl",
      description   = "Level",
      branch        = self.branches.level,
      gainbias      = self.objects.level,
      range         = self.objects.level,
      biasMap       = zeroToOneMap,
      biasPrecision = 2,
      initialBias   = 0.5
    },
    fm = GainBias {
      button        = "FM",
      branch        = self.branches.fm,
      description   = "FM",
      gainbias      = self.objects.fm,
      range         = self.objects.fmRange,
      biasMap       = Encoder.getMap("[-1,1]"),
      biasUnits     = app.unitNone,
      biasPrecision = 2,
      initialBias   = 0.0
    }
  }, {
    expanded  = { "tune", "f0", "source", "push", "modDepth", "syncThreshold", "mirror", "level" },
    collapsed = {}
  }
end

return Mirror
