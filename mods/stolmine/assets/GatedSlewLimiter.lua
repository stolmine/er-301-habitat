local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local Gate = require "Unit.ViewControl.Gate"
local GainBias = require "Unit.ViewControl.GainBias"
local OptionControl = require "Unit.ViewControl.OptionControl"
local Encoder = require "Encoder"

local GatedSlewLimiter = Class {}
GatedSlewLimiter:include(Unit)

function GatedSlewLimiter:init(args)
  args.title = "Gated Slew"
  args.mnemonic = "GS"
  Unit.init(self, args)
end

function GatedSlewLimiter:onLoadGraph(channelCount)
  if channelCount == 2 then
    self:loadStereoGraph()
  else
    self:loadMonoGraph()
  end
end

function GatedSlewLimiter:loadMonoGraph()
  local slew = self:addObject("slew1", libstolmine.GatedSlewLimiter())
  local time = self:addObject("time", app.ParameterAdapter())
  local gate = self:addObject("gate", app.Comparator())
  gate:setGateMode()

  connect(self, "In1", slew, "In")
  connect(gate, "Out", slew, "Gate")
  connect(slew, "Out", self, "Out1")

  tie(slew, "Time", time, "Out")

  self:addMonoBranch("time", time, "In", time, "Out")
  self:addMonoBranch("gate", gate, "In", gate, "Out")
end

function GatedSlewLimiter:loadStereoGraph()
  local slew1 = self:addObject("slew1", libstolmine.GatedSlewLimiter())
  local slew2 = self:addObject("slew2", libstolmine.GatedSlewLimiter())
  local time = self:addObject("time", app.ParameterAdapter())
  local gate = self:addObject("gate", app.Comparator())
  gate:setGateMode()

  connect(self, "In1", slew1, "In")
  connect(self, "In2", slew2, "In")
  connect(gate, "Out", slew1, "Gate")
  connect(gate, "Out", slew2, "Gate")
  connect(slew1, "Out", self, "Out1")
  connect(slew2, "Out", self, "Out2")

  tie(slew1, "Time", time, "Out")
  tie(slew2, "Time", time, "Out")
  tie(slew2, "Direction", slew1, "Direction")

  self:addMonoBranch("time", time, "In", time, "Out")
  self:addMonoBranch("gate", gate, "In", gate, "Out")
end

local views = {
  expanded = {
    "gate",
    "time",
    "dir"
  },
  collapsed = {}
}

function GatedSlewLimiter:onLoadViews(objects, branches)
  local controls = {}

  controls.gate = Gate {
    button = "gate",
    branch = branches.gate,
    description = "Gate",
    comparator = objects.gate
  }

  controls.time = GainBias {
    button = "time",
    branch = branches.time,
    description = "Slew Time",
    gainbias = objects.time,
    range = objects.time,
    biasMap = Encoder.getMap("slewTimes"),
    biasUnits = app.unitSecs,
    initialBias = 1.0,
    scaling = app.octaveScaling,
    gainMap = Encoder.getMap("gain")
  }

  controls.dir = OptionControl {
    button = "o",
    description = "Mode",
    option = objects.slew1:getOption("Direction"),
    choices = {
      "up",
      "both",
      "down"
    }
  }

  return controls, views
end

return GatedSlewLimiter
