local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local ViewControl = require "Unit.ViewControl"
local Zoomable = require "Unit.ViewControl.Zoomable"
local MenuHeader = require "Unit.MenuControl.Header"
local OptionControl = require "Unit.MenuControl.OptionControl"
local Task = require "Unit.MenuControl.Task"
local pool = require "Sample.Pool"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY

-- Write indicator (display only)
local WriteControl = Class {}
WriteControl:include(ViewControl)

function WriteControl:init(args)
  ViewControl.init(self, "write")
  self:setClassName("WriteControl")

  local graphic = libstolmine.WriteIndicator(0, 0, ply, 64)
  graphic:setOption(args.option)
  self:setControlGraphic(graphic)
end

-- Waveform display for gesture buffer (follows RecordingView pattern)
local WaveView = Class {}
WaveView:include(Zoomable)

function WaveView:init(args)
  Zoomable.init(self)
  self:setClassName("GestureWaveView")

  local width = args.width or (4 * ply)
  local head = args.head or app.logError("%s: head is missing from args.", self)
  self.head = head

  local graphic = app.Graphic(0, 0, width, 64)
  self.mainDisplay = libstolmine.GestureHeadDisplay(head, 0, 0, width, 64)
  graphic:addChild(self.mainDisplay)
  self:setMainCursorController(self.mainDisplay)
  self:setControlGraphic(graphic)

  -- Spots for cursor navigation
  for i = 1, (width // ply) do
    self:addSpotDescriptor { center = (i - 0.5) * ply }
  end
  self.verticalDivider = width

  -- Sub display
  self.subGraphic = app.Graphic(0, 0, 128, 64)

  self.subDisplay = app.HeadSubDisplay(head)
  self.subGraphic:addChild(self.subDisplay)

  self.subButton1 = app.SubButton("|<<", 1)
  self.subGraphic:addChild(self.subButton1)

  self.subButton2 = app.SubButton("> / ||", 2)
  self.subGraphic:addChild(self.subButton2)

  self.subButton3 = app.SubButton("", 3)
  self.subGraphic:addChild(self.subButton3)
end

function WaveView:setSample(sample)
  if sample then
    self.subDisplay:setName(sample.name)
  end
end

function WaveView:subReleased(i, shifted)
  if shifted then return false end
  if self.head then
    if i == 1 then
      self.head:reset()
    elseif i == 2 then
      self.head:toggle()
    end
  end
  return true
end

function WaveView:getFloatingMenuItems()
  local choices = Zoomable.getFloatingMenuItems(self)
  choices[#choices + 1] = "collapse"
  return choices
end

function WaveView:onFloatingMenuSelection(choice)
  if choice == "collapse" then
    self:callUp("switchView", "expanded")
  else
    return Zoomable.onFloatingMenuSelection(self, choice)
  end
end

local GestureSeq = Class {}
GestureSeq:include(Unit)

function GestureSeq:init(args)
  args.title = "Gesture"
  args.mnemonic = "Gs"
  Unit.init(self, args)
end

function GestureSeq:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.GestureSeq())

  -- Sink chain input (pure generator)
  local sink = self:addObject("sink", app.ConstantGain())
  sink:hardSet("Gain", 0.0)
  connect(self, "In1", sink, "In")

  -- Run gate (toggle)
  local run = self:addObject("run", app.Comparator())
  run:setToggleMode()
  connect(run, "Out", op, "Run")
  self:addMonoBranch("run", run, "In", run, "Out")

  -- Reset gate
  local reset = self:addObject("reset", app.Comparator())
  reset:setTriggerMode()
  connect(reset, "Out", op, "Reset")
  self:addMonoBranch("reset", reset, "In", reset, "Out")

  -- Offset (CV source)
  local offset = self:addObject("offset", app.ParameterAdapter())
  offset:hardSet("Bias", 0.0)
  tie(op, "Offset", offset, "Out")
  self:addMonoBranch("offset", offset, "In", offset, "Out")

  -- Slew
  local slew = self:addObject("slew", app.ParameterAdapter())
  slew:hardSet("Bias", 0.0)
  tie(op, "Slew", slew, "Out")
  self:addMonoBranch("slew", slew, "In", slew, "Out")

  -- Erase gate
  local erase = self:addObject("erase", app.Comparator())
  erase:setGateMode()
  connect(erase, "Out", op, "Erase")
  self:addMonoBranch("erase", erase, "In", erase, "Out")

  -- Output
  for i = 1, channelCount do
    connect(op, "Out", self, "Out"..i)
  end

  -- Default buffer (5 seconds)
  self:createBuffer(5)
end

function GestureSeq:createBuffer(seconds)
  local sample = pool.create{type = "buffer", channels = 1, secs = seconds}
  if sample then
    self:setSample(sample)
  end
end

function GestureSeq:setSample(sample)
  if self.sample then self.sample:release(self) end
  self.sample = sample
  if self.sample then self.sample:claim(self) end
  if sample then
    self.objects.op:setSample(sample.pSample)
  else
    self.objects.op:setSample(nil)
  end
  self:notifyControls("setSample", sample)
end

function GestureSeq:onRemove()
  if self.sample then
    self.sample:release(self)
    self.sample = nil
  end
  Unit.onRemove(self)
end

function GestureSeq:onShowMenu(objects, branches)
  return {
    bufferHeader = MenuHeader {
      description = "Buffer"
    },
    buf5 = Task {
      description = "5 sec",
      task = function() self:createBuffer(5) end
    },
    buf10 = Task {
      description = "10 sec",
      task = function() self:createBuffer(10) end
    },
    buf20 = Task {
      description = "20 sec",
      task = function() self:createBuffer(20) end
    },
    clearBuffer = Task {
      description = "Clear Buffer",
      task = function()
        if self.sample then
          self.sample.pSample:zero()
          self.sample.pSample:setDirty()
        end
      end
    },
    sensHeader = MenuHeader {
      description = "Write Sensitivity"
    },
    sensitivity = OptionControl {
      description = "Sens.",
      option = objects.op:getOption("Sensitivity"),
      choices = {"Low", "Medium", "High"},
      boolean = true
    }
  }, {"bufferHeader", "buf5", "buf10", "buf20", "clearBuffer", "sensHeader", "sensitivity"}
end

function GestureSeq:serialize()
  local t = Unit.serialize(self)
  if self.sample then
    t.sample = pool.serializeSample(self.sample)
  end
  return t
end

function GestureSeq:deserialize(t)
  Unit.deserialize(self, t)
  if t.sample then
    local sample = pool.deserializeSample(t.sample, self.chain)
    if sample then
      self:setSample(sample)
    end
  end
end

function GestureSeq:onLoadViews()
  local controls = {
    run = Gate {
      button      = "run",
      description = "Run",
      branch      = self.branches.run,
      comparator  = self.objects.run
    },
    reset = Gate {
      button      = "reset",
      description = "Reset",
      branch      = self.branches.reset,
      comparator  = self.objects.reset
    },
    wave = WaveView {
      head = self.objects.op,
      width = 4 * ply
    },
    offset = GainBias {
      button        = "offset",
      description   = "Offset",
      branch        = self.branches.offset,
      gainbias      = self.objects.offset,
      range         = self.objects.offset,
      biasMap       = Encoder.getMap("[-1,1]"),
      biasPrecision = 3,
      initialBias   = 0.0
    },
    slew = GainBias {
      button        = "slew",
      description   = "Slew",
      branch        = self.branches.slew,
      gainbias      = self.objects.slew,
      range         = self.objects.slew,
      biasMap       = Encoder.getMap("[0,10]"),
      biasUnits     = app.unitSecs,
      biasPrecision = 2,
      initialBias   = 0.0
    },
    erase = Gate {
      button      = "erase",
      description = "Erase",
      branch      = self.branches.erase,
      comparator  = self.objects.erase
    },
    write = WriteControl {
      option = self.objects.op:getOption("Write Active")
    }
  }

  local views = {
    expanded  = { "run", "reset", "offset", "slew", "erase", "write" },
    collapsed = {},
    offset    = { "wave", "offset" },
    slew      = { "wave", "slew" },
    erase     = { "wave", "erase" }
  }

  return controls, views
end

return GestureSeq
