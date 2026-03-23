local app = app
local libpeaks = require "peaks.libpeaks"
local Class = require "Base.Class"
local Unit = require "Unit"
local Gate = require "Unit.ViewControl.Gate"

local HighHat = Class {}
HighHat:include(Unit)

function HighHat:init(args)
  args.title = "High Hat"
  args.mnemonic = "HH"
  Unit.init(self, args)
end

function HighHat:onLoadGraph(channelCount)
  local op = self:addObject("op", libpeaks.HighHat())
  connect(op, "Out", self, "Out1")

  local gate = self:addObject("gate", app.Comparator())
  gate:setGateMode()
  connect(gate, "Out", op, "Gate")
  self:addMonoBranch("gate", gate, "In", gate, "Out")
end

function HighHat:onLoadViews()
  return {
    gate = Gate {
      button      = "gate",
      description = "Gate",
      branch      = self.branches.gate,
      comparator  = self.objects.gate
    }
  }, {
    expanded  = { "gate" },
    collapsed = {}
  }
end

return HighHat
