local app = app
local libpeaks = require "peaks.libpeaks"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Encoder = require "Encoder"

local Plo = Class {}
Plo:include(Unit)

function Plo:init(args)
  args.title = "PLO"
  args.mnemonic = "PL"
  Unit.init(self, args)
end

function Plo:onLoadGraph(channelCount)
  local op = self:addObject("op", libpeaks.Plo())
  connect(op, "Out", self, "Out1")

  local clock = self:addObject("clock", app.Comparator())
  clock:setGateMode()
  connect(clock, "Out", op, "Clock")
  self:addMonoBranch("clock", clock, "In", clock, "Out")

  local reset = self:addObject("reset", app.Comparator())
  reset:setTriggerMode()
  connect(reset, "Out", op, "Reset")
  self:addMonoBranch("reset", reset, "In", reset, "Out")

  local p1 = self:addObject("p1", app.ParameterAdapter())
  p1:hardSet("Bias", 0.5)
  tie(op, "Param1", p1, "Out")
  self:addMonoBranch("p1", p1, "In", p1, "Out")

  local p2 = self:addObject("p2", app.ParameterAdapter())
  p2:hardSet("Bias", 0.5)
  tie(op, "Param2", p2, "Out")
  self:addMonoBranch("p2", p2, "In", p2, "Out")

  local p3 = self:addObject("p3", app.ParameterAdapter())
  p3:hardSet("Bias", 0.5)
  tie(op, "Param3", p3, "Out")
  self:addMonoBranch("p3", p3, "In", p3, "Out")

  local p4 = self:addObject("p4", app.ParameterAdapter())
  p4:hardSet("Bias", 0.5)
  tie(op, "Param4", p4, "Out")
  self:addMonoBranch("p4", p4, "In", p4, "Out")
end

function Plo:onLoadViews()
  return {
    clock = Gate {
      button      = "clock",
      description = "Clock",
      branch      = self.branches.clock,
      comparator  = self.objects.clock
    },
    reset = Gate {
      button      = "reset",
      description = "Reset",
      branch      = self.branches.reset,
      comparator  = self.objects.reset
    },
    p1 = GainBias {
      button = "ptch", description = "Pitch",
      branch = self.branches.p1, gainbias = self.objects.p1,
      range = self.objects.p1, biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2, initialBias = 0.5
    },
    p2 = GainBias {
      button = "shape", description = "Shape",
      branch = self.branches.p2, gainbias = self.objects.p2,
      range = self.objects.p2, biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2, initialBias = 0.5
    },
    p3 = GainBias {
      button = "w.rt", description = "WSM Rate",
      branch = self.branches.p3, gainbias = self.objects.p3,
      range = self.objects.p3, biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2, initialBias = 0.5
    },
    p4 = GainBias {
      button = "w.dp", description = "WSM Depth",
      branch = self.branches.p4, gainbias = self.objects.p4,
      range = self.objects.p4, biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2, initialBias = 0.5
    }
  }, {
    expanded  = { "clock", "reset", "p1", "p2", "p3", "p4" },
    collapsed = {}
  }
end

return Plo
