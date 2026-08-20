local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Encoder = require "Encoder"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Fader = require "Unit.ViewControl.Fader"
local OptionControl = require "Unit.ViewControl.OptionControl"
local LaretStepListControl = require "spreadsheet.LaretStepListControl"
local LaretOverviewControl = require "spreadsheet.LaretOverviewControl"
local LaretClockControl = require "spreadsheet.LaretClockControl"
local LaretsMixControl = require "spreadsheet.LaretsMixControl"
local TransformGateControl = require "spreadsheet.TransformGateControl"
local MenuHeader = require "Unit.MenuControl.Header"
local Task = require "Unit.MenuControl.Task"

-- Stepped map for the 7-way Transform function selector. Discrete selectors
-- belong in expansions as stepped faders (decision 2026-08-18).
-- Shared by the TransformGateControl sub-readout AND its expanded fader.
local xformFactorMap = (function()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end)()

local xformFuncMap = (function()
  local m = app.LinearDialMap(0, 6)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(1, 1, 1, 1)
  map:setRounding(1)
  return map
end

local skewMap = floatMap(-1, 1)
local offsetMap = floatMap(-1, 1)
local clockDivMap = intMap(1, 16)
local stepCountMap = intMap(1, 16)

local Larets = Class {}
Larets:include(Unit)

function Larets:init(args)
  args.title = "Larets"
  args.mnemonic = "Lr"
  Unit.init(self, args)
end

function Larets:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Larets())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  local clock = self:addObject("clock", app.Comparator())
  clock:setTriggerMode()
  connect(clock, "Out", op, "Clock")
  self:addMonoBranch("clock", clock, "In", clock, "Out")

  local reset = self:addObject("reset", app.Comparator())
  reset:setTriggerMode()
  connect(reset, "Out", op, "Reset")
  self:addMonoBranch("reset", reset, "In", reset, "Out")

  local xformGate = self:addObject("xformGate", app.Comparator())
  xformGate:setGateMode()
  connect(xformGate, "Out", op, "Transform")
  self:addMonoBranch("xform", xformGate, "In", xformGate, "Out")

  local stepCount = self:addObject("stepCount", app.ParameterAdapter())
  stepCount:hardSet("Bias", 8)
  tie(op, "StepCount", stepCount, "Out")
  self:addMonoBranch("stepCount", stepCount, "In", stepCount, "Out")

  local skew = self:addObject("skew", app.ParameterAdapter())
  skew:hardSet("Bias", 0.0)
  tie(op, "Skew", skew, "Out")
  self:addMonoBranch("skew", skew, "In", skew, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 1.0)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")

  local outputLevel = self:addObject("outputLevel", app.ParameterAdapter())
  outputLevel:hardSet("Bias", 1.0)
  tie(op, "OutputLevel", outputLevel, "Out")
  self:addMonoBranch("outputLevel", outputLevel, "In", outputLevel, "Out")

  local compressAmt = self:addObject("compressAmt", app.ParameterAdapter())
  compressAmt:hardSet("Bias", 0.0)
  tie(op, "CompressAmt", compressAmt, "Out")
  self:addMonoBranch("compressAmt", compressAmt, "In", compressAmt, "Out")

  local clockDiv = self:addObject("clockDiv", app.ParameterAdapter())
  clockDiv:hardSet("Bias", 1)
  tie(op, "ClockDiv", clockDiv, "Out")
  self:addMonoBranch("clockDiv", clockDiv, "In", clockDiv, "Out")

  local loopLength = self:addObject("loopLength", app.ParameterAdapter())
  loopLength:hardSet("Bias", 16)
  tie(op, "LoopLength", loopLength, "Out")
  self:addMonoBranch("loopLength", loopLength, "In", loopLength, "Out")

  local paramOffset = self:addObject("paramOffset", app.ParameterAdapter())
  paramOffset:hardSet("Bias", 0.0)
  tie(op, "ParamOffset", paramOffset, "Out")
  self:addMonoBranch("paramOffset", paramOffset, "In", paramOffset, "Out")

  local xformFunc = self:addObject("xformFunc", app.ParameterAdapter())
  xformFunc:hardSet("Bias", 0)
  tie(op, "TransformFunc", xformFunc, "Out")

  local xformDepth = self:addObject("xformDepth", app.ParameterAdapter())
  xformDepth:hardSet("Bias", 0.5)
  tie(op, "TransformDepth", xformDepth, "Out")
end

function Larets:onLoadViews()
  return {
    clock = LaretClockControl {
      button = "clock",
      description = "Clock",
      branch = self.branches.clock,
      comparator = self.objects.clock,
      resetComparator = self.objects.reset,
      divParam = self.objects.clockDiv:getParameter("Bias")
    },
    steps = LaretStepListControl {
      button = "steps",
      description = "Steps",
      width = app.SECTION_PLY,
      op = self.objects.op
    },
    overview = LaretOverviewControl {
      button = "over",
      description = "Overview",
      branch = self.branches.skew,
      gainbias = self.objects.skew,
      range = self.objects.skew,
      biasMap = skewMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0,
      op = self.objects.op,
      stepCountParam = self.objects.stepCount:getParameter("Bias"),
      loopParam = self.objects.loopLength:getParameter("Bias")
    },
    skew = GainBias {
      button = "skew",
      description = "Skew",
      branch = self.branches.skew,
      gainbias = self.objects.skew,
      range = self.objects.skew,
      biasMap = skewMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    stepCount = GainBias {
      button = "steps",
      description = "Step Count",
      branch = self.branches.stepCount,
      gainbias = self.objects.stepCount,
      range = self.objects.stepCount,
      biasMap = stepCountMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 8
    },
    loopLength = GainBias {
      button = "loop",
      description = "Loop Length",
      branch = self.branches.loopLength,
      gainbias = self.objects.loopLength,
      range = self.objects.loopLength,
      biasMap = (function()
        local m = app.LinearDialMap(1, 16)
        m:setSteps(1, 1, 1, 1)
        m:setRounding(1)
        return m
      end)(),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 16
    },
    offset = GainBias {
      button = "ofs",
      description = "Param Offset",
      branch = self.branches.paramOffset,
      gainbias = self.objects.paramOffset,
      range = self.objects.paramOffset,
      biasMap = offsetMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    xform = TransformGateControl {
      button = "xform",
      description = "Transform",
      seq = self.objects.op,
      comparator = self.objects.xformGate,
      branch = self.branches.xform,
      funcParam = self.objects.xformFunc:getParameter("Bias"),
      factorParam = self.objects.xformDepth:getParameter("Bias"),
      funcNames = { [0] = "all", "t+p", "type", "prm", "tick", "rot", "rev" },
      funcMap = xformFuncMap,
      factorMap = xformFactorMap,
      factorPrecision = 2,
      paramALabel = "depth"
    },
    mix = LaretsMixControl {
      button = "mix",
      description = "Mix",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = Encoder.getMap("unit"),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0,
      outputLevel = self.objects.outputLevel:getParameter("Bias"),
      compressAmt = self.objects.compressAmt:getParameter("Bias"),
      op = self.objects.op
    },
    reset = Gate {
      button = "reset",
      description = "Reset",
      branch = self.branches.reset,
      comparator = self.objects.reset
    },
    clockDiv = GainBias {
      button = "div",
      description = "Clock Division",
      branch = self.branches.clockDiv,
      gainbias = self.objects.clockDiv,
      range = self.objects.clockDiv,
      biasMap = clockDivMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 1
    },
    -- Expansion members. Fader, not GainBias: GainBias REQUIRES a branch, and
    -- adding branches here would bolt new CV inlets onto the unit just to make
    -- ENTER work. Fader takes the parameter directly.
    xformFuncFader = Fader {
      button = "func",
      description = "Xform Func",
      param = self.objects.xformFunc:getParameter("Bias"),
      map = xformFuncMap,
      units = app.unitNone,
      precision = 0
    },
    xformDepthFader = Fader {
      button = "depth",
      description = "Xform Depth",
      param = self.objects.xformDepth:getParameter("Bias"),
      map = xformFactorMap,
      units = app.unitNone,
      precision = 2
    },
    outputLevelFader = Fader {
      button = "out",
      description = "Output Level",
      param = self.objects.outputLevel:getParameter("Bias"),
      -- LaretsMixControl.levelMap spans 0..4; a [0,1] map here would clamp the
      -- fader at a quarter of the range the sub-readout shows.
      map = LaretsMixControl.levelMap,
      units = app.unitNone,
      precision = LaretsMixControl.readoutPrecision
    },
    compressAmtFader = Fader {
      button = "comp",
      description = "Compress",
      param = self.objects.compressAmt:getParameter("Bias"),
      map = LaretsMixControl.compMap,
      units = app.unitNone,
      precision = LaretsMixControl.readoutPrecision
    },
    autoMakeup = OptionControl {
      button = "auto",
      description = "Auto Makeup",
      option = self.objects.op:getOption("AutoMakeup"),
      choices = { "on", "off" }
    },
    stepMode = OptionControl {
      button = "step",
      description = "Step Advance",
      option = self.objects.op:getOption("StepMode"),
      choices = { "seq", "rand" }
    }
  }, {
    expanded = { "clock", "steps", "overview", "offset", "xform", "mix" },
    collapsed = {},
    clock = { "clock", "reset", "clockDiv" },
    -- stepMode added 2026-08-18: the control carries this toggle on SHIFT but
    -- the expansion silently dropped it.
    overview = { "overview", "skew", "stepCount", "loopLength", "stepMode" },
    xform = { "xform", "xformFuncFader", "xformDepthFader" },
    mix = { "mix", "outputLevelFader", "compressAmtFader", "autoMakeup" }
  }
end

function Larets:serialize()
  local t = Unit.serialize(self)
  local op = self.objects.op
  local types, params, ticks = {}, {}, {}
  for i = 0, 15 do
    types[tostring(i)] = op:getStepType(i)
    params[tostring(i)] = op:getStepParam(i)
    ticks[tostring(i)] = op:getStepTicks(i)
  end
  t.stepTypes = types
  t.stepParams = params
  t.stepTicks = ticks
  t.stepCount = self.objects.stepCount:getParameter("Bias"):target()
  t.skew = self.objects.skew:getParameter("Bias"):target()
  t.mix = self.objects.mix:getParameter("Bias"):target()
  t.outputLevel = self.objects.outputLevel:getParameter("Bias"):target()
  t.compressAmt = self.objects.compressAmt:getParameter("Bias"):target()
  t.paramOffset = self.objects.paramOffset:getParameter("Bias"):target()
  t.loopLength = self.objects.loopLength:getParameter("Bias"):target()
  t.clockDiv = self.objects.clockDiv:getParameter("Bias"):target()
  t.xformFunc = self.objects.xformFunc:getParameter("Bias"):target()
  t.xformDepth = self.objects.xformDepth:getParameter("Bias"):target()
  return t
end

function Larets:deserialize(t)
  Unit.deserialize(self, t)
  local op = self.objects.op
  if t.stepTypes then
    for i = 0, 15 do
      local k = tostring(i)
      if t.stepTypes[k] ~= nil then op:setStepType(i, t.stepTypes[k]) end
      if t.stepParams and t.stepParams[k] ~= nil then op:setStepParam(i, t.stepParams[k]) end
      if t.stepTicks and t.stepTicks[k] ~= nil then op:setStepTicks(i, t.stepTicks[k]) end
    end
  end
  if t.stepCount ~= nil then self.objects.stepCount:hardSet("Bias", t.stepCount) end
  if t.skew ~= nil then self.objects.skew:hardSet("Bias", t.skew) end
  if t.mix ~= nil then self.objects.mix:hardSet("Bias", t.mix) end
  if t.outputLevel ~= nil then self.objects.outputLevel:hardSet("Bias", t.outputLevel) end
  if t.compressAmt ~= nil then self.objects.compressAmt:hardSet("Bias", t.compressAmt) end
  if t.paramOffset ~= nil then self.objects.paramOffset:hardSet("Bias", t.paramOffset) end
  if t.loopLength ~= nil then
    -- Old patches stored 0 to mean "all steps"; new semantics use 1-16 with
    -- the wrap clamped to current step count. Migrate 0 to 16 (max = all).
    local v = t.loopLength
    if v < 1 then v = 16 end
    self.objects.loopLength:hardSet("Bias", v)
  end
  if t.clockDiv ~= nil then self.objects.clockDiv:hardSet("Bias", t.clockDiv) end
  if t.xformFunc ~= nil then self.objects.xformFunc:hardSet("Bias", t.xformFunc) end
  if t.xformDepth ~= nil then self.objects.xformDepth:hardSet("Bias", t.xformDepth) end
  op:loadStep(0)
end

function Larets:setAllTicks(len)
  local op = self.objects.op
  for i = 0, 15 do
    op:setStepTicks(i, len)
  end
  if self.controls and self.controls.steps then
    op:loadStep(self.controls.steps.currentStep or 0)
  end
end

function Larets:onShowMenu(objects, branches)
  return {
    tickHeader = MenuHeader { description = "Set All Tick Lengths" },
    tick1 = Task { description = "1 tick", task = function() self:setAllTicks(1) end },
    tick2 = Task { description = "2 ticks", task = function() self:setAllTicks(2) end },
    tick4 = Task { description = "4 ticks", task = function() self:setAllTicks(4) end },
    tick8 = Task { description = "8 ticks", task = function() self:setAllTicks(8) end },
    tick16 = Task { description = "16 ticks", task = function() self:setAllTicks(16) end }
  }, { "tickHeader", "tick1", "tick2", "tick4", "tick8", "tick16" }
end

return Larets
