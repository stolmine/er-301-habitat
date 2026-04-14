local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local HelicaseOverviewControl = require "spreadsheet.HelicaseOverviewControl"
local HelicaseShapingControl = require "spreadsheet.HelicaseShapingControl"
local HelicaseModControl = require "spreadsheet.HelicaseModControl"
local HelicaseSyncControl = require "spreadsheet.HelicaseSyncControl"
local OptionControl = require "Unit.ViewControl.OptionControl"
local Encoder = require "Encoder"
local MenuHeader = require "Unit.MenuControl.Header"
local OptionMenu = require "Unit.MenuControl.OptionControl"

local ply = app.SECTION_PLY

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local f0Map = (function()
  local m = app.LinearDialMap(0.1, 2000)
  m:setSteps(100, 10, 1, 0.1)
  return m
end)()

local levelMap = floatMap(0, 1)

local Helicase = Class {}
Helicase:include(Unit)

function Helicase:init(args)
  args.title = "Helicase"
  args.mnemonic = "Hx"
  Unit.init(self, args)
end

function Helicase:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Helicase())

  -- Sink chain input (generator)
  local sink = self:addObject("sink", app.ConstantGain())
  sink:hardSet("Gain", 0.0)
  connect(self, "In1", sink, "In")

  -- V/Oct (10x scaling, Plaits pattern)
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  local voctGain = self:addObject("voctGain", app.ConstantGain())
  voctGain:hardSet("Gain", 10.0)
  connect(tune, "Out", voctGain, "In")
  connect(voctGain, "Out", op, "V/Oct")
  connect(tune, "Out", tuneRange, "In")

  -- Sync
  local syncComparator = self:addObject("syncComparator", app.Comparator())
  syncComparator:setTriggerMode()
  connect(syncComparator, "Out", op, "Sync")
  self:addMonoBranch("sync", syncComparator, "In", syncComparator, "Out")

  -- Sync phase threshold
  local syncThreshold = self:addObject("syncThreshold", app.ParameterAdapter())
  syncThreshold:hardSet("Bias", 0.0)
  tie(op, "SyncThreshold", syncThreshold, "Out")
  self:addMonoBranch("syncThreshold", syncThreshold, "In", syncThreshold, "Out")

  -- Output
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  -- Fundamental
  local f0 = self:addObject("f0", app.ParameterAdapter())
  f0:hardSet("Bias", 110.0)
  tie(op, "Fundamental", f0, "Out")
  self:addMonoBranch("f0", f0, "In", f0, "Out")

  -- Mod Mix (overview ply hidden fader)
  local modMix = self:addObject("modMix", app.ParameterAdapter())
  modMix:hardSet("Bias", 0.5)
  tie(op, "ModMix", modMix, "Out")
  self:addMonoBranch("modMix", modMix, "In", modMix, "Out")

  -- Carrier Shape
  local carrierShape = self:addObject("carrierShape", app.ParameterAdapter())
  carrierShape:hardSet("Bias", 0.0)
  tie(op, "CarrierShape", carrierShape, "Out")
  self:addMonoBranch("carrierShape", carrierShape, "In", carrierShape, "Out")

  -- Mod Index (shaping ply hidden fader)
  local modIndex = self:addObject("modIndex", app.ParameterAdapter())
  modIndex:hardSet("Bias", 1.0)
  tie(op, "ModIndex", modIndex, "Out")
  self:addMonoBranch("modIndex", modIndex, "In", modIndex, "Out")

  -- Disc Index
  local discIndex = self:addObject("discIndex", app.ParameterAdapter())
  discIndex:hardSet("Bias", 0.0)
  tie(op, "DiscIndex", discIndex, "Out")
  self:addMonoBranch("discIndex", discIndex, "In", discIndex, "Out")

  -- Disc Type
  local discType = self:addObject("discType", app.ParameterAdapter())
  discType:hardSet("Bias", 0.0)
  tie(op, "DiscType", discType, "Out")
  self:addMonoBranch("discType", discType, "In", discType, "Out")

  -- Ratio (mod ply hidden fader)
  local ratio = self:addObject("ratio", app.ParameterAdapter())
  ratio:hardSet("Bias", 2.0)
  tie(op, "Ratio", ratio, "Out")
  self:addMonoBranch("ratio", ratio, "In", ratio, "Out")

  -- Feedback
  local feedback = self:addObject("feedback", app.ParameterAdapter())
  feedback:hardSet("Bias", 0.0)
  tie(op, "Feedback", feedback, "Out")
  self:addMonoBranch("feedback", feedback, "In", feedback, "Out")

  -- Mod Shape
  local modShape = self:addObject("modShape", app.ParameterAdapter())
  modShape:hardSet("Bias", 0.0)
  tie(op, "ModShape", modShape, "Out")
  self:addMonoBranch("modShape", modShape, "In", modShape, "Out")

  -- Fine (expansion only)
  local fine = self:addObject("fine", app.ParameterAdapter())
  fine:hardSet("Bias", 0.0)
  tie(op, "Fine", fine, "Out")
  self:addMonoBranch("fine", fine, "In", fine, "Out")

  -- Level
  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Bias", 0.5)
  tie(op, "Level", level, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")

  -- V/Oct branch
  self:addMonoBranch("tune", tune, "In", tune, "Out")
end

function Helicase:onLoadViews()
  local controls = {}
  local views = {
    expanded = { "tune", "f0", "overview", "shaping", "modulator", "sync", "level" },
    collapsed = {}
  }

  controls.tune = Pitch {
    button = "V/Oct",
    description = "V/Oct",
    branch = self.branches.tune,
    offset = self.objects.tune,
    range = self.objects.tuneRange
  }

  controls.f0 = GainBias {
    button = "f0",
    description = "Fundamental",
    branch = self.branches.f0,
    gainbias = self.objects.f0,
    range = self.objects.f0,
    biasMap = f0Map,
    biasUnits = app.unitHertz,
    biasPrecision = 1,
    initialBias = 110.0
  }

  local modMixMap = floatMap(0, 1)
  controls.overview = HelicaseOverviewControl {
    button = "over",
    description = "Overview",
    branch = self.branches.modMix,
    gainbias = self.objects.modMix,
    range = self.objects.modMix,
    biasMap = modMixMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5,
    helicase = self.objects.op,
    carrierShapeParam = self.objects.carrierShape:getParameter("Bias")
  }

  -- Overview expansion: modMix + carrierShape + lin/expo
  views.overview = { "overview", "overModMix", "overCarrierShape", "overLinExpo" }
  controls.overModMix = GainBias {
    button = "mix",
    description = "Mod Mix",
    branch = self.branches.modMix,
    gainbias = self.objects.modMix,
    range = self.objects.modMix,
    biasMap = modMixMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }
  local carrShapeMap = (function()
    local m = app.LinearDialMap(0, 7)
    m:setSteps(1, 0.1, 0.01, 0.001)
    return m
  end)()
  controls.overCarrierShape = GainBias {
    button = "carr",
    description = "Carrier Shape",
    branch = self.branches.carrierShape,
    gainbias = self.objects.carrierShape,
    range = self.objects.carrierShape,
    biasMap = carrShapeMap,
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 0.0
  }

  controls.overLinExpo = OptionControl {
    button = "FM",
    description = "FM Mode",
    option = self.objects.op:getOption("LinExpo"),
    choices = { "lin", "exp" }
  }

  local modIndexMap = floatMap(0, 10)
  controls.shaping = HelicaseShapingControl {
    button = "shape",
    description = "Shaping",
    branch = self.branches.modIndex,
    gainbias = self.objects.modIndex,
    range = self.objects.modIndex,
    biasMap = modIndexMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0,
    helicase = self.objects.op,
    discIndexParam = self.objects.discIndex:getParameter("Bias"),
    discTypeParam = self.objects.discType:getParameter("Bias")
  }

  local ratioMap = (function()
    local m = app.LinearDialMap(0.5, 16)
    m:setSteps(1, 0.5, 0.1, 0.01)
    return m
  end)()
  controls.modulator = HelicaseModControl {
    button = "mod",
    description = "Modulator",
    branch = self.branches.ratio,
    gainbias = self.objects.ratio,
    range = self.objects.ratio,
    biasMap = ratioMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 2.0,
    helicase = self.objects.op,
    feedbackParam = self.objects.feedback:getParameter("Bias"),
    modShapeParam = self.objects.modShape:getParameter("Bias")
  }

  -- Expansion: mod + ratio + feedback + shape + fine
  views.modulator = { "modulator", "modRatio", "modFeedback", "modShape", "modFine" }
  controls.modRatio = GainBias {
    button = "ratio",
    description = "Ratio",
    branch = self.branches.ratio,
    gainbias = self.objects.ratio,
    range = self.objects.ratio,
    biasMap = ratioMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 2.0
  }
  controls.modFeedback = GainBias {
    button = "fdbk",
    description = "Feedback",
    branch = self.branches.feedback,
    gainbias = self.objects.feedback,
    range = self.objects.feedback,
    biasMap = floatMap(0, 1),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  local shapeMap = (function()
    local m = app.LinearDialMap(0, 7)
    m:setSteps(1, 0.1, 0.01, 0.001)
    return m
  end)()
  controls.modShape = GainBias {
    button = "shape",
    description = "Mod Shape",
    branch = self.branches.modShape,
    gainbias = self.objects.modShape,
    range = self.objects.modShape,
    biasMap = shapeMap,
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 0.0
  }

  local fineMap = (function()
    local m = app.LinearDialMap(-100, 100)
    m:setSteps(10, 1, 0.1, 0.1)
    return m
  end)()
  controls.modFine = GainBias {
    button = "fine",
    description = "Fine Tune",
    branch = self.branches.fine,
    gainbias = self.objects.fine,
    range = self.objects.fine,
    biasMap = fineMap,
    biasUnits = app.unitCents,
    biasPrecision = 1,
    initialBias = 0.0
  }

  controls.sync = HelicaseSyncControl {
    button = "sync",
    description = "Sync",
    branch = self.branches.sync,
    comparator = self.objects.syncComparator,
    phaseThresholdParam = self.objects.syncThreshold:getParameter("Bias")
  }

  -- Sync expansion: sync + phase threshold GainBias (CV-modulatable)
  views.sync = { "sync", "syncPhase" }
  controls.syncPhase = GainBias {
    button = "phase",
    description = "Phase Threshold",
    branch = self.branches.syncThreshold,
    gainbias = self.objects.syncThreshold,
    range = self.objects.syncThreshold,
    biasMap = floatMap(0, 1),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.level = GainBias {
    button = "level",
    description = "Level",
    branch = self.branches.level,
    gainbias = self.objects.level,
    range = self.objects.level,
    biasMap = levelMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }

  -- Shaping expansion: shaping + modIndex + discIndex + discType
  views.shaping = { "shaping", "shapModIndex", "shapDiscIndex", "shapDiscType" }
  controls.shapModIndex = GainBias {
    button = "index",
    description = "Mod Index",
    branch = self.branches.modIndex,
    gainbias = self.objects.modIndex,
    range = self.objects.modIndex,
    biasMap = modIndexMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }
  controls.shapDiscIndex = GainBias {
    button = "disc",
    description = "Disc Index",
    branch = self.branches.discIndex,
    gainbias = self.objects.discIndex,
    range = self.objects.discIndex,
    biasMap = floatMap(0, 1),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }
  local discTypeMap = (function()
    local m = app.LinearDialMap(0, 15)
    m:setSteps(1, 0.1, 0.01, 0.001)
    return m
  end)()
  controls.shapDiscType = GainBias {
    button = "type",
    description = "Disc Type",
    branch = self.branches.discType,
    gainbias = self.objects.discType,
    range = self.objects.discType,
    biasMap = discTypeMap,
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 0.0
  }

  return controls, views
end

function Helicase:onShowMenu(objects, branches)
  return {
    modeHeader = MenuHeader { description = "Mode" },
    hifi = OptionMenu {
      description = "Quality",
      option = objects.op:getOption("HiFi"),
      choices = { "lo-fi", "hi-fi" }
    }
  }, { "modeHeader", "hifi" }
end

local adapterBiases = {
  "syncThreshold", "f0", "modMix", "carrierShape", "modIndex",
  "discIndex", "discType", "ratio", "feedback", "modShape",
  "fine", "level"
}

function Helicase:serialize()
  local t = Unit.serialize(self)
  for _, name in ipairs(adapterBiases) do
    local obj = self.objects[name]
    if obj then
      t[name] = obj:getParameter("Bias"):target()
    end
  end
  if self.objects.tune then
    t.tuneOffset = self.objects.tune:getParameter("Offset"):target()
  end
  if self.objects.syncComparator then
    t.syncCompThreshold = self.objects.syncComparator:getParameter("Threshold"):target()
  end
  return t
end

function Helicase:deserialize(t)
  Unit.deserialize(self, t)
  for _, name in ipairs(adapterBiases) do
    if t[name] ~= nil and self.objects[name] then
      self.objects[name]:hardSet("Bias", t[name])
    end
  end
  if t.tuneOffset ~= nil and self.objects.tune then
    self.objects.tune:hardSet("Offset", t.tuneOffset)
  end
  if t.syncCompThreshold ~= nil and self.objects.syncComparator then
    self.objects.syncComparator:hardSet("Threshold", t.syncCompThreshold)
  end
  -- Refresh lin/exp label so on-screen state matches restored option
  if self.controls and self.controls.overview
      and self.controls.overview.updateLinExpo then
    self.controls.overview:updateLinExpo()
  end
end

return Helicase
