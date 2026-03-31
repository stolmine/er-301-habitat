local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local SegmentListControl = require "stolmine.SegmentListControl"
local TransferCurveControl = require "stolmine.TransferCurveControl"
local ModeSelector = require "stolmine.ModeSelector"
local Encoder = require "Encoder"

local MenuHeader = require "Unit.MenuControl.Header"
local Task = require "Unit.MenuControl.Task"

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(4, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local inputMap = floatMap(-1, 1)
local skewMap = floatMap(0.1, 4.0)
local segCountMap = intMap(4, 32)
local levelMap = floatMap(-1, 1)
local deviationMap = floatMap(0, 1)

local devScopeMap = (function()
  local m = app.LinearDialMap(0, 3)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local Etcher = Class {}
Etcher:include(Unit)

function Etcher:init(args)
  args.title = "Etcher"
  args.mnemonic = "Et"
  Unit.init(self, args)
end

function Etcher:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.Etcher())

  -- Input: GainBias adapter connected to op's inlet
  local input = self:addObject("input", app.ParameterAdapter())
  input:hardSet("Bias", 0.0)
  connect(input, "Out", op, "Input")
  self:addMonoBranch("input", input, "In", input, "Out")

  -- Skew
  local skew = self:addObject("skew", app.ParameterAdapter())
  skew:hardSet("Bias", 1.0)
  tie(op, "Skew", skew, "Out")
  self:addMonoBranch("skew", skew, "In", skew, "Out")

  -- Level
  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Bias", 1.0)
  tie(op, "Level", level, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")

  -- Segment count
  local segCount = self:addObject("segCount", app.ParameterAdapter())
  segCount:hardSet("Bias", 16)
  tie(op, "SegmentCount", segCount, "Out")
  self:addMonoBranch("segCount", segCount, "In", segCount, "Out")

  -- Deviation
  local deviation = self:addObject("deviation", app.ParameterAdapter())
  deviation:hardSet("Bias", 0.0)
  tie(op, "Deviation", deviation, "Out")
  self:addMonoBranch("deviation", deviation, "In", deviation, "Out")

  -- Deviation scope
  local devScope = self:addObject("devScope", app.ParameterAdapter())
  devScope:hardSet("Bias", 0)
  tie(op, "DeviationScope", devScope, "Out")
  self:addMonoBranch("devScope", devScope, "In", devScope, "Out")

  -- Output
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end
end

-- Preset helpers

function Etcher:setPresetLinearRamp()
  local op = self.objects.op
  local segCount = op:getSegmentCount()
  for i = 0, segCount - 1 do
    local v = -1.0 + 2.0 * i / (segCount - 1)
    op:setSegmentOffset(i, v)
    op:setSegmentCurve(i, 1) -- linear
    op:setSegmentWeight(i, 1.0)
  end
  self:reloadEditBuffer()
end

function Etcher:setPresetSCurve()
  local op = self.objects.op
  local segCount = op:getSegmentCount()
  for i = 0, segCount - 1 do
    local t = i / (segCount - 1)
    -- Smoothstep S-curve: 3t^2 - 2t^3, mapped to -5..+5
    local s = 3 * t * t - 2 * t * t * t
    op:setSegmentOffset(i, -1.0 + 2.0 * s)
    op:setSegmentCurve(i, 2) -- cubic
    op:setSegmentWeight(i, 1.0)
  end
  self:reloadEditBuffer()
end

function Etcher:setPresetStaircase()
  local op = self.objects.op
  local segCount = op:getSegmentCount()
  for i = 0, segCount - 1 do
    local v = -1.0 + 2.0 * i / (segCount - 1)
    op:setSegmentOffset(i, v)
    op:setSegmentCurve(i, 0) -- step (none)
    op:setSegmentWeight(i, 1.0)
  end
  self:reloadEditBuffer()
end

function Etcher:setPresetRandom()
  local op = self.objects.op
  local segCount = op:getSegmentCount()
  for i = 0, segCount - 1 do
    op:setSegmentOffset(i, (math.random() * 2 - 1) * 1.0)
    op:setSegmentCurve(i, 1) -- linear
    op:setSegmentWeight(i, 1.0)
  end
  self:reloadEditBuffer()
end

function Etcher:clearAllSegments()
  local op = self.objects.op
  for i = 0, 31 do
    op:setSegmentOffset(i, 0.0)
    op:setSegmentCurve(i, 1) -- linear
    op:setSegmentWeight(i, 1.0)
  end
  self:reloadEditBuffer()
end

function Etcher:reloadEditBuffer()
  local op = self.objects.op
  if self.controls and self.controls.segments then
    op:loadSegment(self.controls.segments.currentSegment or 0)
  else
    op:loadSegment(0)
  end
end

function Etcher:onShowMenu(objects, branches)
  local controls = {}

  controls.presetHeader = MenuHeader {
    description = "Presets"
  }
  controls.linearRamp = Task {
    description = "Linear ramp",
    task = function() self:setPresetLinearRamp() end
  }
  controls.sCurve = Task {
    description = "S-curve",
    task = function() self:setPresetSCurve() end
  }
  controls.staircase = Task {
    description = "Staircase (quantizer)",
    task = function() self:setPresetStaircase() end
  }
  controls.random = Task {
    description = "Random",
    task = function() self:setPresetRandom() end
  }

  controls.clearHeader = MenuHeader {
    description = "Clear"
  }
  controls.clear = Task {
    description = "Reset all segments",
    task = function() self:clearAllSegments() end
  }

  return controls, {
    "presetHeader",
    "linearRamp", "sCurve", "staircase", "random",
    "clearHeader",
    "clear"
  }
end

function Etcher:onLoadViews()
  return {
    input = GainBias {
      button = "input",
      description = "Input",
      branch = self.branches.input,
      gainbias = self.objects.input,
      range = self.objects.input,
      biasMap = inputMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    segments = SegmentListControl {
      description = "Segments",
      width = app.SECTION_PLY,
      etcher = self.objects.op
    },
    curve = TransferCurveControl {
      etcher = self.objects.op,
      width = 2 * app.SECTION_PLY,
      deviation = self.objects.deviation:getParameter("Bias"),
      deviationScope = self.objects.devScope:getParameter("Bias"),
      segCount = self.objects.segCount:getParameter("Bias")
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
      initialBias = 1.0
    },
    segCount = GainBias {
      button = "segs",
      description = "Segments",
      branch = self.branches.segCount,
      gainbias = self.objects.segCount,
      range = self.objects.segCount,
      biasMap = segCountMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 16
    },
    level = GainBias {
      button = "level",
      description = "Level",
      branch = self.branches.level,
      gainbias = self.objects.level,
      range = self.objects.level,
      biasMap = levelMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    deviation = GainBias {
      button = "dev",
      description = "Deviation",
      branch = self.branches.deviation,
      gainbias = self.objects.deviation,
      range = self.objects.deviation,
      biasMap = deviationMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    devScope = ModeSelector {
      button = "scope",
      description = "Dev Scope",
      branch = self.branches.devScope,
      gainbias = self.objects.devScope,
      range = self.objects.devScope,
      biasMap = devScopeMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = { [0] = "ofst", "crv", "wgt", "all" }
    }
  }, {
    expanded = { "input", "segments", "curve", "skew", "level" },
    collapsed = {},
    curve = { "curve", "deviation", "devScope", "segCount" }
  }
end

function Etcher:serialize()
  local t = Unit.serialize(self)
  local op = self.objects.op
  local segments = {}
  for i = 0, 31 do
    segments[tostring(i)] = {
      offset = op:getSegmentOffset(i),
      curve = op:getSegmentCurve(i),
      weight = op:getSegmentWeight(i)
    }
  end
  t.segments = segments
  t.skew = self.objects.skew:getParameter("Bias"):target()
  t.level = self.objects.level:getParameter("Bias"):target()
  t.segCount = self.objects.segCount:getParameter("Bias"):target()
  t.deviation = self.objects.deviation:getParameter("Bias"):target()
  t.devScope = self.objects.devScope:getParameter("Bias"):target()
  return t
end

function Etcher:deserialize(t)
  Unit.deserialize(self, t)
  if t.segments then
    local op = self.objects.op
    for i = 0, 31 do
      local s = t.segments[tostring(i)]
      if s then
        op:setSegmentOffset(i, s.offset or 0.0)
        op:setSegmentCurve(i, s.curve or 1)
        op:setSegmentWeight(i, s.weight or 1.0)
      end
    end
  end
  if t.skew ~= nil then
    self.objects.skew:hardSet("Bias", t.skew)
  end
  if t.level ~= nil then
    self.objects.level:hardSet("Bias", t.level)
  end
  if t.segCount ~= nil then
    self.objects.segCount:hardSet("Bias", t.segCount)
  end
  if t.deviation ~= nil then
    self.objects.deviation:hardSet("Bias", t.deviation)
  end
  if t.devScope ~= nil then
    self.objects.devScope:hardSet("Bias", t.devScope)
  end
  -- Sync edit buffer after framework restores stale edit params
  self.objects.op:loadSegment(0)
end

return Etcher
