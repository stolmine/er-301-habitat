local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local TapListControl = require "stolmine.TapListControl"
local Encoder = require "Encoder"

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(4, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local mixMap = floatMap(0, 1)
local timeMap = floatMap(0.01, 2.0)
local feedbackMap = floatMap(0, 0.95)
local tapCountMap = intMap(1, 16)
local skewMap = floatMap(-2, 2)

local MultitapDelay = Class {}
MultitapDelay:include(Unit)

function MultitapDelay:init(args)
  args.title = "Raindrops"
  args.mnemonic = "RD"
  Unit.init(self, args)
end

function MultitapDelay:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.MultitapDelay())

  -- Allocate 2 seconds of delay buffer
  op:allocateTimeUpTo(2.0)

  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In")
    connect(op, "OutR", self, "Out2")
  end

  local function tieParam(name, adapter)
    tie(op, name, adapter, "Out")
  end

  -- Master time
  local masterTime = self:addObject("masterTime", app.ParameterAdapter())
  masterTime:hardSet("Bias", 0.5)
  tieParam("MasterTime", masterTime)
  self:addMonoBranch("masterTime", masterTime, "In", masterTime, "Out")

  -- Feedback
  local feedback = self:addObject("feedback", app.ParameterAdapter())
  feedback:hardSet("Bias", 0.3)
  tieParam("Feedback", feedback)
  self:addMonoBranch("feedback", feedback, "In", feedback, "Out")

  -- Mix
  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 0.5)
  tieParam("Mix", mix)
  self:addMonoBranch("mix", mix, "In", mix, "Out")

  -- Tap count
  local tapCount = self:addObject("tapCount", app.ParameterAdapter())
  tapCount:hardSet("Bias", 4)
  tieParam("TapCount", tapCount)
  self:addMonoBranch("tapCount", tapCount, "In", tapCount, "Out")

  -- Skew
  local skew = self:addObject("skew", app.ParameterAdapter())
  skew:hardSet("Bias", 0.0)
  tieParam("Skew", skew)
  self:addMonoBranch("skew", skew, "In", skew, "Out")
end

function MultitapDelay:onLoadViews()
  return {
    taps = TapListControl {
      description = "Taps",
      width = app.SECTION_PLY,
      delay = self.objects.op
    },
    masterTime = GainBias {
      button = "time",
      description = "Master Time",
      branch = self.branches.masterTime,
      gainbias = self.objects.masterTime,
      range = self.objects.masterTime,
      biasMap = timeMap,
      biasUnits = app.unitSecs,
      biasPrecision = 2,
      initialBias = 0.5
    },
    feedback = GainBias {
      button = "fdbk",
      description = "Feedback",
      branch = self.branches.feedback,
      gainbias = self.objects.feedback,
      range = self.objects.feedback,
      biasMap = feedbackMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.3
    },
    mix = GainBias {
      button = "mix",
      description = "Mix",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = mixMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    tapCount = GainBias {
      button = "taps",
      description = "Tap Count",
      branch = self.branches.tapCount,
      gainbias = self.objects.tapCount,
      range = self.objects.tapCount,
      biasMap = tapCountMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 4
    },
    skew = GainBias {
      button = "skew",
      description = "Skew",
      branch = self.branches.skew,
      gainbias = self.objects.skew,
      range = self.objects.skew,
      biasMap = skewMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    }
  }, {
    expanded = { "taps", "masterTime", "feedback", "mix", "tapCount", "skew" },
    collapsed = {}
  }
end

function MultitapDelay:serialize()
  local t = Unit.serialize(self)
  local op = self.objects.op
  local taps = {}
  for i = 0, 15 do
    taps[tostring(i)] = {
      time = op:getTapTime(i),
      level = op:getTapLevel(i),
      pan = op:getTapPan(i),
      filterCutoff = op:getFilterCutoff(i),
      filterQ = op:getFilterQ(i),
      filterType = op:getFilterType(i)
    }
  end
  t.taps = taps
  return t
end

function MultitapDelay:deserialize(t)
  Unit.deserialize(self, t)
  if t.taps then
    local op = self.objects.op
    for i = 0, 15 do
      local tap = t.taps[tostring(i)]
      if tap then
        op:setTapTime(i, tap.time or 0.5)
        op:setTapLevel(i, tap.level or 0.0)
        op:setTapPan(i, tap.pan or 0.0)
        op:setFilterCutoff(i, tap.filterCutoff or 0.8)
        op:setFilterQ(i, tap.filterQ or 0.0)
        op:setFilterType(i, tap.filterType or 0)
      end
    end
  end
  self.objects.op:loadTap(0)
  self.objects.op:loadFilter(0)
end

return MultitapDelay
