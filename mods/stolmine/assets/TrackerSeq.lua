local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local StepListControl = require "stolmine.StepListControl"
local SeqInfoControl = require "stolmine.SeqInfoControl"
local TransformGateControl = require "stolmine.TransformGateControl"
local ModeSelector = require "stolmine.ModeSelector"
local Encoder = require "Encoder"

local MenuHeader = require "Unit.MenuControl.Header"
local Task = require "Unit.MenuControl.Task"

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(4, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local seqLenMap = intMap(1, 64)
local loopLenMap = intMap(0, 64)
local scopeMap = (function()
  local m = app.LinearDialMap(0, 3)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local TrackerSeq = Class {}
TrackerSeq:include(Unit)

function TrackerSeq:init(args)
  args.title = "Excel"
  args.mnemonic = "Ex"
  Unit.init(self, args)
end

function TrackerSeq:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.TrackerSeq())

  -- Clock
  local clock = self:addObject("clock", app.Comparator())
  clock:setGateMode()
  connect(clock, "Out", op, "Clock")
  self:addMonoBranch("clock", clock, "In", clock, "Out")

  -- Reset
  local reset = self:addObject("reset", app.Comparator())
  reset:setGateMode()
  connect(reset, "Out", op, "Reset")
  self:addMonoBranch("reset", reset, "In", reset, "Out")

  -- Slew
  local slew = self:addObject("slew", app.ParameterAdapter())
  slew:hardSet("Bias", 0.0)
  tie(op, "Slew", slew, "Out")
  self:addMonoBranch("slew", slew, "In", slew, "Out")

  -- SeqLength
  local seqLength = self:addObject("seqLength", app.ParameterAdapter())
  seqLength:hardSet("Bias", 16)
  tie(op, "SeqLength", seqLength, "Out")
  self:addMonoBranch("seqLength", seqLength, "In", seqLength, "Out")

  -- LoopLength
  local loopLength = self:addObject("loopLength", app.ParameterAdapter())
  loopLength:hardSet("Bias", 0)
  tie(op, "LoopLength", loopLength, "Out")
  self:addMonoBranch("loopLength", loopLength, "In", loopLength, "Out")

  -- Transform gate
  local xformGate = self:addObject("xformGate", app.Comparator())
  xformGate:setGateMode()
  connect(xformGate, "Out", op, "Transform")
  self:addMonoBranch("xform", xformGate, "In", xformGate, "Out")

  -- Transform function and factor
  local xformFunc = self:addObject("xformFunc", app.ParameterAdapter())
  xformFunc:hardSet("Bias", 0)
  tie(op, "TransformFunc", xformFunc, "Out")

  local xformFactor = self:addObject("xformFactor", app.ParameterAdapter())
  xformFactor:hardSet("Bias", 1)
  tie(op, "TransformFactor", xformFactor, "Out")

  -- Transform scope
  local xformScope = self:addObject("xformScope", app.ParameterAdapter())
  xformScope:hardSet("Bias", 0)
  tie(op, "TransformScope", xformScope, "Out")
  self:addMonoBranch("xformScope", xformScope, "In", xformScope, "Out")

  -- Output
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end
end

function TrackerSeq:setAllStepLengths(len)
  local op = self.objects.op
  for i = 0, 63 do
    op:setStepLength(i, len)
  end
end

function TrackerSeq:randomizeOffsets()
  local op = self.objects.op
  local is10v = self.offsetRange10v ~= false
  local range = is10v and 5 or 1
  for i = 0, 63 do
    op:setStepOffset(i, (math.random() * 2 - 1) * range)
  end
  -- Reload current step into edit buffer
  if self.controls and self.controls.steps then
    op:loadStep(self.controls.steps.currentStep or 0)
  end
end

function TrackerSeq:clearAllOffsets()
  local op = self.objects.op
  for i = 0, 63 do
    op:setStepOffset(i, 0.0)
  end
  if self.controls and self.controls.steps then
    op:loadStep(self.controls.steps.currentStep or 0)
  end
end

function TrackerSeq:onShowMenu(objects, branches)
  local controls = {}

  controls.stepLenHeader = MenuHeader {
    description = "Set All Step Lengths"
  }
  controls.stepLen1 = Task {
    description = "1 tick",
    task = function() self:setAllStepLengths(1) end
  }
  controls.stepLen2 = Task {
    description = "2 ticks",
    task = function() self:setAllStepLengths(2) end
  }
  controls.stepLen4 = Task {
    description = "4 ticks",
    task = function() self:setAllStepLengths(4) end
  }

  controls.rangeHeader = MenuHeader {
    description = "Offset Range"
  }
  controls.range10v = Task {
    description = "10Vpp (-5 to +5)",
    task = function()
      self.offsetRange10v = true
      self.controls.steps:setOffsetRange(true)
      self.objects.op:getParameter("OffsetRange"):hardSet(5.0)
    end
  }
  controls.range2v = Task {
    description = "2Vpp (-1 to +1)",
    task = function()
      self.offsetRange10v = false
      self.controls.steps:setOffsetRange(false)
      self.objects.op:getParameter("OffsetRange"):hardSet(1.0)
    end
  }

  controls.offsetHeader = MenuHeader {
    description = "Offsets"
  }
  controls.randomize = Task {
    description = "Randomize all offsets",
    task = function() self:randomizeOffsets() end
  }
  controls.clearOffsets = Task {
    description = "Clear all offsets",
    task = function() self:clearAllOffsets() end
  }

  controls.snapshotHeader = MenuHeader {
    description = "Snapshot"
  }
  controls.snapshotSave = Task {
    description = "Save snapshot",
    task = function() self.objects.op:snapshotSave() end
  }
  controls.snapshotRestore = Task {
    description = "Restore snapshot",
    task = function() self.objects.op:snapshotRestore() end
  }

  return controls, {
    "stepLenHeader",
    "stepLen1", "stepLen2", "stepLen4",
    "rangeHeader",
    "range10v", "range2v",
    "offsetHeader",
    "randomize", "clearOffsets",
    "snapshotHeader",
    "snapshotSave", "snapshotRestore"
  }
end

function TrackerSeq:onLoadViews()
  return {
    steps = StepListControl {
      description = "Steps",
      width = app.SECTION_PLY,
      seq = self.objects.op
    },
    info = SeqInfoControl {
      description = "Sequence",
      width = app.SECTION_PLY,
      seq = self.objects.op,
      seqLength = self.objects.seqLength:getParameter("Bias"),
      loopLength = self.objects.loopLength:getParameter("Bias"),
      transformScope = self.objects.xformScope:getParameter("Bias")
    },
    clock = Gate {
      button = "clock",
      description = "Clock",
      branch = self.branches.clock,
      comparator = self.objects.clock
    },
    reset = Gate {
      button = "reset",
      description = "Reset",
      branch = self.branches.reset,
      comparator = self.objects.reset
    },
    slew = GainBias {
      button = "slew",
      description = "Slew Time",
      branch = self.branches.slew,
      gainbias = self.objects.slew,
      range = self.objects.slew,
      biasMap = Encoder.getMap("[0,10]"),
      biasUnits = app.unitSecs,
      biasPrecision = 3,
      initialBias = 0.0
    },
    xform = TransformGateControl {
      button = "xform",
      description = "Transform",
      seq = self.objects.op,
      comparator = self.objects.xformGate,
      branch = self.branches.xform,
      funcParam = self.objects.xformFunc:getParameter("Bias"),
      factorParam = self.objects.xformFactor:getParameter("Bias")
    },
    -- Expanded view controls (visible when info ply is focused)
    seqLen = GainBias {
      button = "len",
      description = "Seq Length",
      branch = self.branches.seqLength,
      gainbias = self.objects.seqLength,
      range = self.objects.seqLength,
      biasMap = seqLenMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 16
    },
    loopLen = GainBias {
      button = "loop",
      description = "Loop Length",
      branch = self.branches.loopLength,
      gainbias = self.objects.loopLength,
      range = self.objects.loopLength,
      biasMap = loopLenMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0
    },
    xformScope = ModeSelector {
      button = "scope",
      description = "Xform Scope",
      branch = self.branches.xformScope,
      gainbias = self.objects.xformScope,
      range = self.objects.xformScope,
      biasMap = scopeMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = { [0] = "ofst", "len", "dev", "all" }
    }
  }, {
    expanded = { "steps", "info", "clock", "reset", "slew", "xform" },
    collapsed = {},
    info = { "info", "seqLen", "loopLen", "xformScope" }
  }
end

function TrackerSeq:serialize()
  local t = Unit.serialize(self)
  local op = self.objects.op
  local steps = {}
  for i = 0, 63 do
    steps[tostring(i)] = {
      offset = op:getStepOffset(i),
      length = op:getStepLength(i),
      deviation = op:getStepDeviation(i)
    }
  end
  t.steps = steps
  t.offsetRange10v = self.offsetRange10v ~= false
  t.xformFunc = self.objects.xformFunc:getParameter("Bias"):target()
  t.xformFactor = self.objects.xformFactor:getParameter("Bias"):target()
  t.xformScope = self.objects.xformScope:getParameter("Bias"):target()
  return t
end

function TrackerSeq:deserialize(t)
  Unit.deserialize(self, t)
  if t.steps then
    local op = self.objects.op
    for i = 0, 63 do
      local s = t.steps[tostring(i)]
      if s then
        op:setStepOffset(i, s.offset or 0.0)
        op:setStepLength(i, s.length or 1)
        op:setStepDeviation(i, s.deviation or s.slew or 0.0)
      end
    end
  end
  if t.offsetRange10v ~= nil then
    self.offsetRange10v = t.offsetRange10v
    local range = t.offsetRange10v and 5.0 or 1.0
    self.objects.op:getParameter("OffsetRange"):hardSet(range)
    if self.controls and self.controls.steps then
      self.controls.steps:setOffsetRange(t.offsetRange10v)
    end
  end
  if t.xformFunc ~= nil then
    self.objects.xformFunc:hardSet("Bias", t.xformFunc)
  end
  if t.xformFactor ~= nil then
    self.objects.xformFactor:hardSet("Bias", t.xformFactor)
  end
  if t.xformScope ~= nil then
    self.objects.xformScope:hardSet("Bias", t.xformScope)
  end
  -- Sync edit buffer with step 0 after framework restores stale edit params
  self.objects.op:loadStep(0)
  -- Update func label on xform control
  if self.controls and self.controls.xform then
    local val = math.floor(self.objects.xformFunc:getParameter("Bias"):target() + 0.5)
    local name = self.controls.xform.funcNames[val]
    if name then self.controls.xform.funcLabel:setText(name) end
  end
end

return TrackerSeq
