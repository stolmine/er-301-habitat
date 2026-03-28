local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local MenuHeader = require "Unit.MenuControl.Header"
local OptionControl = require "Unit.MenuControl.OptionControl"
local Task = require "Unit.MenuControl.Task"
local Encoder = require "Encoder"

local GestureSeq = Class {}
GestureSeq:include(Unit)

function GestureSeq:init(args)
  args.title = "Gesture"
  args.mnemonic = "Gs"
  Unit.init(self, args)
end

function GestureSeq:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.GestureSeq())

  -- Run from chain input
  connect(self, "In1", op, "Run")

  -- Reset gate
  local reset = self:addObject("reset", app.Comparator())
  reset:setGateMode()
  connect(reset, "Out", op, "Reset")
  self:addMonoBranch("reset", reset, "In", reset, "Out")

  -- Offset (CV source)
  local offset = self:addObject("offset", app.ParameterAdapter())
  offset:hardSet("Bias", 0.0)
  tie(op, "Offset", offset, "Out")
  self:addMonoBranch("offset", offset, "In", offset, "Out")

  -- Write gate
  local write = self:addObject("write", app.Comparator())
  write:setGateMode()
  connect(write, "Out", op, "Write")
  self:addMonoBranch("write", write, "In", write, "Out")

  -- Output
  for i = 1, channelCount do
    connect(op, "Out", self, "Out"..i)
  end
end

function GestureSeq:onShowMenu(objects, branches)
  return {
    bufferHeader = MenuHeader {
      description = "Buffer"
    },
    bufferSize = OptionControl {
      description = "Buffer Size",
      option = objects.op:getOption("Buffer Size"),
      choices = {"5 sec", "10 sec", "20 sec"}
    },
    clearBuffer = Task {
      description = "Clear Buffer",
      task = function() self.objects.op:clear() end
    }
  }, {"bufferHeader", "bufferSize", "clearBuffer"}
end

function GestureSeq:onLoadViews()
  return {
    reset = Gate {
      button      = "reset",
      description = "Reset",
      branch      = self.branches.reset,
      comparator  = self.objects.reset
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
    write = Gate {
      button      = "write",
      description = "Write",
      branch      = self.branches.write,
      comparator  = self.objects.write
    }
  }, {
    expanded  = { "reset", "offset", "write" },
    collapsed = {}
  }
end

return GestureSeq
