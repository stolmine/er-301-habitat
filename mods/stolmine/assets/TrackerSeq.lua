local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local StepListControl = require "stolmine.StepListControl"
local SeqInfoControl = require "stolmine.SeqInfoControl"
local TransformGateControl = require "stolmine.TransformGateControl"
local Encoder = require "Encoder"

local MenuHeader = require "Unit.MenuControl.Header"
local Task = require "Unit.MenuControl.Task"

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
      description = "Slew",
      branch = self.branches.slew,
      gainbias = self.objects.slew,
      range = self.objects.slew,
      biasMap = Encoder.getMap("[0,1]"),
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
      factorParam = self.objects.xformFactor:getParameter("Bias")
    }
  }, {
    expanded = { "steps", "info", "clock", "reset", "slew", "xform" },
    collapsed = {}
  }
end

function TrackerSeq:onSerialize()
  local op = self.objects.op
  local steps = {}
  for i = 0, 63 do
    steps[tostring(i)] = {
      offset = op:getStepOffset(i),
      length = op:getStepLength(i),
      deviation = op:getStepDeviation(i)
    }
  end
  return { steps = steps }
end

function TrackerSeq:onDeserialize(t)
  if t and t.steps then
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
end

return TrackerSeq
