local app = app
local libstolmine = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local ChaselightControl = require "spreadsheet.ChaselightControl"
local GateSeqInfoControl = require "spreadsheet.GateSeqInfoControl"
local RatchetControl = require "spreadsheet.RatchetControl"
local TransformGateControl = require "spreadsheet.TransformGateControl"
local ModeSelector = require "spreadsheet.ModeSelector"
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

local GateSeqUnit = Class {}
GateSeqUnit:include(Unit)

function GateSeqUnit:init(args)
  args.title = "Ballot"
  args.mnemonic = "Bl"
  Unit.init(self, args)
end

function GateSeqUnit:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.GateSeq())

  -- Clock (gate control, pure generator)
  local clock = self:addObject("clock", app.Comparator())
  clock:setGateMode()
  connect(clock, "Out", op, "Clock")
  self:addMonoBranch("clock", clock, "In", clock, "Out")

  -- Reset
  local reset = self:addObject("reset", app.Comparator())
  reset:setGateMode()
  connect(reset, "Out", op, "Reset")
  self:addMonoBranch("reset", reset, "In", reset, "Out")

  -- Ratchet gate
  local ratchetGate = self:addObject("ratchetGate", app.Comparator())
  ratchetGate:setGateMode()
  connect(ratchetGate, "Out", op, "Ratchet")
  self:addMonoBranch("ratchet", ratchetGate, "In", ratchetGate, "Out")

  -- Ratchet mult
  local ratchetMult = self:addObject("ratchetMult", app.ParameterAdapter())
  ratchetMult:hardSet("Bias", 1)
  tie(op, "RatchetMult", ratchetMult, "Out")

  -- Transform gate
  local xformGate = self:addObject("xformGate", app.Comparator())
  xformGate:setGateMode()
  connect(xformGate, "Out", op, "Transform")
  self:addMonoBranch("xform", xformGate, "In", xformGate, "Out")

  -- Transform params
  local xformFunc = self:addObject("xformFunc", app.ParameterAdapter())
  xformFunc:hardSet("Bias", 0)
  tie(op, "TransformFunc", xformFunc, "Out")

  local xformParamA = self:addObject("xformParamA", app.ParameterAdapter())
  xformParamA:hardSet("Bias", 4)
  tie(op, "TransformParamA", xformParamA, "Out")

  local xformParamB = self:addObject("xformParamB", app.ParameterAdapter())
  xformParamB:hardSet("Bias", 0)
  tie(op, "TransformParamB", xformParamB, "Out")

  -- Transform scope
  local xformScope = self:addObject("xformScope", app.ParameterAdapter())
  xformScope:hardSet("Bias", 0)
  tie(op, "TransformScope", xformScope, "Out")
  self:addMonoBranch("xformScope", xformScope, "In", xformScope, "Out")

  -- Seq length
  local seqLength = self:addObject("seqLength", app.ParameterAdapter())
  seqLength:hardSet("Bias", 16)
  tie(op, "SeqLength", seqLength, "Out")
  self:addMonoBranch("seqLength", seqLength, "In", seqLength, "Out")

  -- Loop length
  local loopLength = self:addObject("loopLength", app.ParameterAdapter())
  loopLength:hardSet("Bias", 0)
  tie(op, "LoopLength", loopLength, "Out")
  self:addMonoBranch("loopLength", loopLength, "In", loopLength, "Out")

  -- Gate width
  local gateWidth = self:addObject("gateWidth", app.ParameterAdapter())
  gateWidth:hardSet("Bias", 0.5)
  tie(op, "GateWidth", gateWidth, "Out")
  self:addMonoBranch("gateWidth", gateWidth, "In", gateWidth, "Out")

  -- Output
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end
end

function GateSeqUnit:onShowMenu(objects, branches)
  local controls = {}

  controls.stepLenHeader = MenuHeader { description = "Set All Gate Lengths" }
  controls.stepLen1 = Task { description = "1 tick", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepLength(i, 1) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }
  controls.stepLen2 = Task { description = "2 ticks", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepLength(i, 2) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }
  controls.stepLen4 = Task { description = "4 ticks", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepLength(i, 4) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }

  controls.velHeader = MenuHeader { description = "Set All Velocities" }
  controls.vel25 = Task { description = "25%", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepVelocity(i, 0.25) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }
  controls.vel50 = Task { description = "50%", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepVelocity(i, 0.5) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }
  controls.vel100 = Task { description = "100%", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepVelocity(i, 1.0) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }

  controls.randomHeader = MenuHeader { description = "Randomize" }
  controls.randomGates = Task { description = "Randomize gates", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepGate(i, math.random() > 0.5) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }
  controls.randomLengths = Task { description = "Randomize lengths", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepLength(i, math.random(1, 4)) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }
  controls.randomVelocities = Task { description = "Randomize velocities", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepVelocity(i, math.random()) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }

  controls.clearHeader = MenuHeader { description = "Clear / Reset" }
  controls.clearGates = Task { description = "Clear all gates", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepGate(i, false) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }
  controls.clearLengths = Task { description = "Reset lengths to 1", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepLength(i, 1) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }
  controls.clearVelocities = Task { description = "Reset velocities to 100%", task = function()
    local op = self.objects.op
    for i = 0, 63 do op:setStepVelocity(i, 1.0) end
    if self.controls and self.controls.steps then
      op:loadStep(self.controls.steps.currentStep or 0)
    end
  end }

  controls.snapshotHeader = MenuHeader { description = "Snapshot" }
  controls.snapshotSave = Task { description = "Save snapshot", task = function()
    self.objects.op:snapshotSave()
  end }
  controls.snapshotRestore = Task { description = "Restore snapshot", task = function()
    self.objects.op:snapshotRestore()
  end }

  return controls, {
    "stepLenHeader", "stepLen1", "stepLen2", "stepLen4",
    "velHeader", "vel25", "vel50", "vel100",
    "randomHeader", "randomGates", "randomLengths", "randomVelocities",
    "clearHeader", "clearGates", "clearLengths", "clearVelocities",
    "snapshotHeader", "snapshotSave", "snapshotRestore"
  }
end

function GateSeqUnit:onLoadViews()
  return {
    steps = ChaselightControl {
      description = "Steps",
      width = app.SECTION_PLY,
      seq = self.objects.op
    },
    info = GateSeqInfoControl {
      description = "Sequence",
      width = app.SECTION_PLY,
      seq = self.objects.op,
      seqLength = self.objects.seqLength:getParameter("Bias"),
      loopLength = self.objects.loopLength:getParameter("Bias"),
      gateWidth = self.objects.gateWidth:getParameter("Bias")
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
    ratchet = RatchetControl {
      button = "ratch",
      description = "Ratchet",
      seq = self.objects.op,
      comparator = self.objects.ratchetGate,
      branch = self.branches.ratchet,
      multParam = self.objects.ratchetMult:getParameter("Bias"),
      lenToggleOption = self.objects.op:getOption("RatchetLen"),
      velToggleOption = self.objects.op:getOption("RatchetVel")
    },
    xform = TransformGateControl {
      button = "xform",
      description = "Transform",
      seq = self.objects.op,
      comparator = self.objects.xformGate,
      branch = self.branches.xform,
      funcParam = self.objects.xformFunc:getParameter("Bias"),
      factorParam = self.objects.xformParamA:getParameter("Bias"),
      paramBParam = self.objects.xformParamB:getParameter("Bias"),
      paramALabel = "prm A",
      paramBLabel = "prm B",
      funcNames = { [0] = "euc", "nr", "grd", "nkl", "inv", "rot", "den" },
      funcMap = (function()
        local m = app.LinearDialMap(0, 6)
        m:setSteps(1, 1, 1, 1)
        m:setRounding(1)
        return m
      end)()
    },
    -- Expanded view controls
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
    gateWidthFader = GainBias {
      button = "width",
      description = "Gate Width",
      branch = self.branches.gateWidth,
      gainbias = self.objects.gateWidth,
      range = self.objects.gateWidth,
      biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias = 0.5
    }
  }, {
    expanded = { "steps", "info", "clock", "reset", "ratchet", "xform" },
    collapsed = {},
    info = { "info", "seqLen", "loopLen", "gateWidthFader" }
  }
end

function GateSeqUnit:serialize()
  local t = Unit.serialize(self)
  local op = self.objects.op
  local steps = {}
  for i = 0, 63 do
    steps[tostring(i)] = {
      gate = op:getStepGate(i),
      length = op:getStepLength(i),
      velocity = op:getStepVelocity(i)
    }
  end
  t.steps = steps
  t.ratchetLen = op:getOption("RatchetLen"):value()
  t.ratchetVel = op:getOption("RatchetVel"):value()
  t.ratchetMult = self.objects.ratchetMult:getParameter("Bias"):target()
  t.xformFunc = self.objects.xformFunc:getParameter("Bias"):target()
  t.xformParamA = self.objects.xformParamA:getParameter("Bias"):target()
  t.xformParamB = self.objects.xformParamB:getParameter("Bias"):target()
  t.xformScope = self.objects.xformScope:getParameter("Bias"):target()
  return t
end

function GateSeqUnit:deserialize(t)
  Unit.deserialize(self, t)
  if t.steps then
    local op = self.objects.op
    for i = 0, 63 do
      local s = t.steps[tostring(i)]
      if s then
        op:setStepGate(i, s.gate or false)
        op:setStepLength(i, s.length or 1)
        op:setStepVelocity(i, s.velocity or 1.0)
      end
    end
  end
  local op = self.objects.op
  if t.ratchetLen ~= nil then op:getOption("RatchetLen"):set(t.ratchetLen) end
  if t.ratchetVel ~= nil then op:getOption("RatchetVel"):set(t.ratchetVel) end
  if t.ratchetMult ~= nil then
    self.objects.ratchetMult:hardSet("Bias", t.ratchetMult)
  end
  if t.xformFunc ~= nil then
    self.objects.xformFunc:hardSet("Bias", t.xformFunc)
  end
  if t.xformParamA ~= nil then
    self.objects.xformParamA:hardSet("Bias", t.xformParamA)
  end
  if t.xformParamB ~= nil then
    self.objects.xformParamB:hardSet("Bias", t.xformParamB)
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
  -- Refresh ratchet toggle labels so on-screen state matches restored options
  if self.controls and self.controls.ratchet then
    self.controls.ratchet:updateToggleLabels()
  end
end

return GateSeqUnit
