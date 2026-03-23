local app = app
local libpeaks = require "peaks.libpeaks"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Encoder = require "Encoder"

local ModSequencer = Class {}
ModSequencer:include(Unit)

function ModSequencer:init(args)
  args.title = "Mod Sequencer"
  args.mnemonic = "MQ"
  Unit.init(self, args)
end

function ModSequencer:onLoadGraph(channelCount)
  local op = self:addObject("op", libpeaks.ModSequencer())
  connect(op, "Out", self, "Out1")

  local gate = self:addObject("gate", app.Comparator())
  gate:setGateMode()
  connect(gate, "Out", op, "Gate")
  self:addMonoBranch("gate", gate, "In", gate, "Out")

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

function ModSequencer:onLoadViews()
  return {
    gate = Gate {
      button      = "gate",
      description = "Gate",
      branch      = self.branches.gate,
      comparator  = self.objects.gate
    },
    p1 = GainBias {
      button = "stp1", description = "Step 1",
      branch = self.branches.p1, gainbias = self.objects.p1,
      range = self.objects.p1, biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2, initialBias = 0.5
    },
    p2 = GainBias {
      button = "stp2", description = "Step 2",
      branch = self.branches.p2, gainbias = self.objects.p2,
      range = self.objects.p2, biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2, initialBias = 0.5
    },
    p3 = GainBias {
      button = "stp3", description = "Step 3",
      branch = self.branches.p3, gainbias = self.objects.p3,
      range = self.objects.p3, biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2, initialBias = 0.5
    },
    p4 = GainBias {
      button = "stp4", description = "Step 4",
      branch = self.branches.p4, gainbias = self.objects.p4,
      range = self.objects.p4, biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2, initialBias = 0.5
    }
  }, {
    expanded  = { "gate", "p1", "p2", "p3", "p4" },
    collapsed = {}
  }
end

return ModSequencer
