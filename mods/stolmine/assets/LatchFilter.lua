local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local OptionControl = require "Unit.MenuControl.OptionControl"
local Encoder = require "Encoder"

local cutoffMap = (function()
  local map = app.LinearDialMap(20, 20000)
  map:setSteps(100, 10, 1, 0.1)
  return map
end)()

local LatchFilter = Class {}
LatchFilter:include(Unit)

function LatchFilter:init(args)
  args.title = "Latch Filter"
  args.mnemonic = "LF"
  Unit.init(self, args)
end

function LatchFilter:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.LatchFilter())
  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")

  local cutoff = self:addObject("cutoff", app.ParameterAdapter())
  cutoff:hardSet("Bias", 1000.0)
  tie(op, "Cutoff", cutoff, "Out")
  self:addMonoBranch("cutoff", cutoff, "In", cutoff, "Out")

  local resonance = self:addObject("resonance", app.ParameterAdapter())
  resonance:hardSet("Bias", 0.5)
  tie(op, "Resonance", resonance, "Out")
  self:addMonoBranch("resonance", resonance, "In", resonance, "Out")
end

function LatchFilter:onShowMenu(objects)
  return {
    mode = OptionControl {
      description = "Mode",
      option      = objects.op:getOption("Mode"),
      choices     = { "LP", "HP" },
      boolean     = true
    }
  }, { "mode" }
end

function LatchFilter:onLoadViews()
  return {
    cutoff = GainBias {
      button        = "freq",
      description   = "Cutoff",
      branch        = self.branches.cutoff,
      gainbias      = self.objects.cutoff,
      range         = self.objects.cutoff,
      biasMap       = cutoffMap,
      biasUnits     = app.unitHertz,
      biasPrecision = 0,
      initialBias   = 1000.0
    },
    resonance = GainBias {
      button        = "res",
      description   = "Resonance",
      branch        = self.branches.resonance,
      gainbias      = self.objects.resonance,
      range         = self.objects.resonance,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    }
  }, {
    expanded  = { "cutoff", "resonance" },
    collapsed = {}
  }
end

return LatchFilter
