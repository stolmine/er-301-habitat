local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local BranchMeter = require "Unit.ViewControl.BranchMeter"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local FadeMixer = Class {}
FadeMixer:include(Unit)

function FadeMixer:init(args)
  args.title = "Fade Mixer"
  args.mnemonic = "FM"
  Unit.init(self, args)
end

function FadeMixer:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.FadeMixer())
  local fade = self:addObject("fade", app.ParameterAdapter())
  local level = self:addObject("level", app.ParameterAdapter())
  fade:hardSet("Bias", 0.0)
  level:hardSet("Bias", 1.0)

  -- 4 input channels with gain staging
  local gain1 = self:addObject("gain1", app.ConstantGain())
  local gain2 = self:addObject("gain2", app.ConstantGain())
  local gain3 = self:addObject("gain3", app.ConstantGain())
  local gain4 = self:addObject("gain4", app.ConstantGain())
  gain1:setClampInDecibels(-59.9); gain1:hardSet("Gain", 1.0)
  gain2:setClampInDecibels(-59.9); gain2:hardSet("Gain", 1.0)
  gain3:setClampInDecibels(-59.9); gain3:hardSet("Gain", 1.0)
  gain4:setClampInDecibels(-59.9); gain4:hardSet("Gain", 1.0)

  connect(gain1, "Out", op, "In1")
  connect(gain2, "Out", op, "In2")
  connect(gain3, "Out", op, "In3")
  connect(gain4, "Out", op, "In4")

  -- Chain passthrough + crossfaded mix
  local sum = self:addObject("sum", app.Sum())
  connect(self, "In1", sum, "Left")
  connect(op, "Out", sum, "Right")
  connect(sum, "Out", self, "Out1")
  if channelCount > 1 then
    connect(sum, "Out", self, "Out2")
  end

  tie(op, "Fade", fade, "Out")
  tie(op, "Level", level, "Out")

  self:addMonoBranch("ch1", gain1, "In", gain1, "Out")
  self:addMonoBranch("ch2", gain2, "In", gain2, "Out")
  self:addMonoBranch("ch3", gain3, "In", gain3, "Out")
  self:addMonoBranch("ch4", gain4, "In", gain4, "Out")
  self:addMonoBranch("fade", fade, "In", fade, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")
end

local views = {
  expanded = { "ch1", "ch2", "ch3", "ch4", "fade", "level" },
  collapsed = {}
}

local function fadeMap()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.25, 0.1, 0.01, 0.001)
  return m
end

local function levelMap()
  local m = app.LinearDialMap(0, 4)
  m:setSteps(1, 0.1, 0.01, 0.001)
  return m
end

function FadeMixer:onLoadViews(objects, branches)
  local controls = {}

  controls.ch1 = BranchMeter {
    button = "in1",
    branch = branches.ch1,
    faderParam = objects.gain1:getParameter("Gain")
  }

  controls.ch2 = BranchMeter {
    button = "in2",
    branch = branches.ch2,
    faderParam = objects.gain2:getParameter("Gain")
  }

  controls.ch3 = BranchMeter {
    button = "in3",
    branch = branches.ch3,
    faderParam = objects.gain3:getParameter("Gain")
  }

  controls.ch4 = BranchMeter {
    button = "in4",
    branch = branches.ch4,
    faderParam = objects.gain4:getParameter("Gain")
  }

  controls.fade = GainBias {
    button = "fade",
    branch = branches.fade,
    description = "Fade",
    gainbias = objects.fade,
    range = objects.fade,
    biasMap = fadeMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.level = GainBias {
    button = "level",
    branch = branches.level,
    description = "Level",
    gainbias = objects.level,
    range = objects.level,
    biasMap = levelMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }

  return controls, views
end

return FadeMixer
