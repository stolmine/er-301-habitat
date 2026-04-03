local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Pitch = require "Unit.ViewControl.Pitch"
local MixControl = require "stolmine.MixControl"
local TimeControl = require "stolmine.TimeControl"
local FeedbackControl = require "stolmine.FeedbackControl"
local TapListControl = require "stolmine.TapListControl"
local FilterListControl = require "stolmine.FilterListControl"
local MacroControl = require "stolmine.MacroControl"
local TransformGateControl = require "stolmine.TransformGateControl"
local RaindropControl = require "stolmine.RaindropControl"
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

-- Macro preset names
local volMacroNames = {
  [0] = "full", "off", "20%", "40%", "60%", "80%", "asc", "desc", "even", "odd", "sine"
}
local panMacroNames = {
  [0] = "cntr", "left", "rght", "L>R", "R>L", "e.L", "o.L", "c.1", "c.2", "c.4", "c.8"
}
local cutoffMacroNames = {
  [0] = "dflt", "asc", "desc", "even", "odd", "sine"
}
local typeMacroNames = {
  [0] = "off", "LP", "BP", "HP", "ntch",
  "e.LP", "e.BP", "e.HP", "o.LP", "o.BP", "o.HP",
  "cycl", "c2LB", "c2LH", "c4", "c8"
}

local qMacroNames = {
  [0] = "off", "20%", "40%", "60%", "80%", "full", "asc", "desc", "even", "odd", "sine"
}

local xformTargetNames = {
  [0] = "all", "taps", "delay", "filt",
  "level", "pan", "pitch", "cut", "Q", "type",
  "time", "fdbk", "tone", "skew", "grain", "count",
  "reset"
}

local mixMap = floatMap(0, 1)
local timeMap = floatMap(0.01, 2.0)
local feedbackMap = floatMap(0, 0.95)
local feedbackToneMap = floatMap(-1, 1)
local tapCountMap = intMap(1, 16)
local skewMap = floatMap(-2, 2)
local grainSizeMap = floatMap(0, 1)
local inputLevelMap = floatMap(0, 4)
local outputLevelMap = floatMap(0, 4)
local tanhMap = floatMap(0, 1)

local MultitapDelay = Class {}
MultitapDelay:include(Unit)

function MultitapDelay:init(args)
  args.title = "Petrichor"
  args.mnemonic = "Pt"
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

  -- Feedback tone
  local feedbackTone = self:addObject("feedbackTone", app.ParameterAdapter())
  feedbackTone:hardSet("Bias", 0.0)
  tieParam("FeedbackTone", feedbackTone)
  self:addMonoBranch("feedbackTone", feedbackTone, "In", feedbackTone, "Out")

  -- V/Oct pitch (ConstantOffset -> ParameterAdapter for tie compatibility)
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  local vOctAdapter = self:addObject("vOctAdapter", app.ParameterAdapter())
  vOctAdapter:hardSet("Gain", 1.0)
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", vOctAdapter, "In")
  tieParam("VOctPitch", vOctAdapter)
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  -- Macro adapters (UI-only, no C++ tie)
  local volMacro = self:addObject("volMacro", app.ParameterAdapter())
  volMacro:hardSet("Bias", 0)
  self:addMonoBranch("volMacro", volMacro, "In", volMacro, "Out")

  local panMacro = self:addObject("panMacro", app.ParameterAdapter())
  panMacro:hardSet("Bias", 0)
  self:addMonoBranch("panMacro", panMacro, "In", panMacro, "Out")

  local cutoffMacro = self:addObject("cutoffMacro", app.ParameterAdapter())
  cutoffMacro:hardSet("Bias", 0)
  self:addMonoBranch("cutoffMacro", cutoffMacro, "In", cutoffMacro, "Out")

  local qMacro = self:addObject("qMacro", app.ParameterAdapter())
  qMacro:hardSet("Bias", 0)
  self:addMonoBranch("qMacro", qMacro, "In", qMacro, "Out")

  local typeMacro = self:addObject("typeMacro", app.ParameterAdapter())
  typeMacro:hardSet("Bias", 0)
  self:addMonoBranch("typeMacro", typeMacro, "In", typeMacro, "Out")

  -- Xform gate
  local xformGate = self:addObject("xformGate", app.Comparator())
  xformGate:setTriggerMode()
  connect(xformGate, "Out", op, "XformGate")
  self:addMonoBranch("xformGate", xformGate, "In", xformGate, "Out")

  local xformTarget = self:addObject("xformTarget", app.ParameterAdapter())
  xformTarget:hardSet("Bias", 0)
  tieParam("XformTarget", xformTarget)
  self:addMonoBranch("xformTarget", xformTarget, "In", xformTarget, "Out")

  local xformDepth = self:addObject("xformDepth", app.ParameterAdapter())
  xformDepth:hardSet("Bias", 0.5)
  tieParam("XformDepth", xformDepth)
  self:addMonoBranch("xformDepth", xformDepth, "In", xformDepth, "Out")

  local xformSpread = self:addObject("xformSpread", app.ParameterAdapter())
  xformSpread:hardSet("Bias", 0.5)
  tieParam("XformSpread", xformSpread)
  self:addMonoBranch("xformSpread", xformSpread, "In", xformSpread, "Out")

  -- Grain size
  local grainSize = self:addObject("grainSize", app.ParameterAdapter())
  grainSize:hardSet("Bias", 0.5)
  tieParam("GrainSize", grainSize)
  self:addMonoBranch("grainSize", grainSize, "In", grainSize, "Out")

  -- Skew
  local skew = self:addObject("skew", app.ParameterAdapter())
  skew:hardSet("Bias", 0.0)
  tieParam("Skew", skew)
  self:addMonoBranch("skew", skew, "In", skew, "Out")

  -- Input level
  local inputLevel = self:addObject("inputLevel", app.ParameterAdapter())
  inputLevel:hardSet("Bias", 1.0)
  tieParam("InputLevel", inputLevel)
  self:addMonoBranch("inputLevel", inputLevel, "In", inputLevel, "Out")

  -- Output level
  local outputLevel = self:addObject("outputLevel", app.ParameterAdapter())
  outputLevel:hardSet("Bias", 1.0)
  tieParam("OutputLevel", outputLevel)
  self:addMonoBranch("outputLevel", outputLevel, "In", outputLevel, "Out")

  -- Tanh
  local tanhAmt = self:addObject("tanhAmt", app.ParameterAdapter())
  tanhAmt:hardSet("Bias", 0.0)
  tieParam("TanhAmt", tanhAmt)
  self:addMonoBranch("tanhAmt", tanhAmt, "In", tanhAmt, "Out")

  -- Pass top-level Bias refs to C++ for gate-triggered randomization
  op:setTopLevelBias(0, masterTime:getParameter("Bias"))
  op:setTopLevelBias(1, feedback:getParameter("Bias"))
  op:setTopLevelBias(2, feedbackTone:getParameter("Bias"))
  op:setTopLevelBias(3, skew:getParameter("Bias"))
  op:setTopLevelBias(4, grainSize:getParameter("Bias"))
  op:setTopLevelBias(5, tapCount:getParameter("Bias"))
end

function MultitapDelay:applyVolumeMacro(value)
  local op = self.objects.op
  local n = op:getTapCount()
  for i = 0, n - 1 do
    local t = (n > 1) and (i / (n - 1)) or 0
    if value == 0 then op:setTapLevel(i, 1.0)                              -- full
    elseif value == 1 then op:setTapLevel(i, 0.0)                          -- off
    elseif value == 2 then op:setTapLevel(i, 0.2)                          -- 20%
    elseif value == 3 then op:setTapLevel(i, 0.4)                          -- 40%
    elseif value == 4 then op:setTapLevel(i, 0.6)                          -- 60%
    elseif value == 5 then op:setTapLevel(i, 0.8)                          -- 80%
    elseif value == 6 then op:setTapLevel(i, (i + 1) / n)                  -- ascending
    elseif value == 7 then op:setTapLevel(i, 1.0 - i / n)                  -- descending
    elseif value == 8 then op:setTapLevel(i, (i % 2 == 0) and 1.0 or 0.0) -- evens
    elseif value == 9 then op:setTapLevel(i, (i % 2 == 1) and 1.0 or 0.0) -- odds
    elseif value == 10 then op:setTapLevel(i, math.sin(t * math.pi))       -- sine
    end
  end
  op:loadTap(self.controls and self.controls.taps and self.controls.taps.currentTap or 0)
end

function MultitapDelay:applyPanMacro(value)
  local op = self.objects.op
  local n = op:getTapCount()
  for i = 0, n - 1 do
    local t = (n > 1) and (i / (n - 1)) or 0
    if value == 0 then op:setTapPan(i, 0.0)                                        -- center
    elseif value == 1 then op:setTapPan(i, -1.0)                                   -- left
    elseif value == 2 then op:setTapPan(i, 1.0)                                    -- right
    elseif value == 3 then op:setTapPan(i, -1.0 + 2.0 * t)                         -- L>R
    elseif value == 4 then op:setTapPan(i, 1.0 - 2.0 * t)                          -- R>L
    elseif value == 5 then op:setTapPan(i, (i % 2 == 0) and -1.0 or 1.0)           -- evens L
    elseif value == 6 then op:setTapPan(i, (i % 2 == 1) and -1.0 or 1.0)           -- odds L
    elseif value == 7 then op:setTapPan(i, (i % 2 == 0) and -1.0 or 1.0)           -- cluster 1
    elseif value == 8 then op:setTapPan(i, (math.floor(i / 2) % 2 == 0) and -1.0 or 1.0) -- cluster 2
    elseif value == 9 then op:setTapPan(i, (math.floor(i / 4) % 2 == 0) and -1.0 or 1.0) -- cluster 4
    elseif value == 10 then op:setTapPan(i, (math.floor(i / 8) % 2 == 0) and -1.0 or 1.0) -- cluster 8
    end
  end
  op:loadTap(self.controls and self.controls.taps and self.controls.taps.currentTap or 0)
end

function MultitapDelay:applyCutoffMacro(value)
  local op = self.objects.op
  local n = op:getTapCount()
  for i = 0, n - 1 do
    local t = (n > 1) and (i / (n - 1)) or 0
    if value == 0 then op:setFilterCutoff(i, 10000)                                -- default
    elseif value == 1 then op:setFilterCutoff(i, 200 + t * 9800)                   -- ascending
    elseif value == 2 then op:setFilterCutoff(i, 10000 - t * 9800)                 -- descending
    elseif value == 3 then op:setFilterCutoff(i, (i % 2 == 0) and 10000 or 200)    -- evens
    elseif value == 4 then op:setFilterCutoff(i, (i % 2 == 1) and 10000 or 200)    -- odds
    elseif value == 5 then op:setFilterCutoff(i, 200 + math.sin(t * math.pi) * 9800) -- sine
    end
  end
  op:loadFilter(self.controls and self.controls.filters and self.controls.filters.currentTap or 0)
end

function MultitapDelay:applyQMacro(value)
  local op = self.objects.op
  local n = op:getTapCount()
  for i = 0, n - 1 do
    local t = (n > 1) and (i / (n - 1)) or 0
    if value == 0 then op:setFilterQ(i, 0.0)                              -- off
    elseif value == 1 then op:setFilterQ(i, 0.2)                          -- 20%
    elseif value == 2 then op:setFilterQ(i, 0.4)                          -- 40%
    elseif value == 3 then op:setFilterQ(i, 0.6)                          -- 60%
    elseif value == 4 then op:setFilterQ(i, 0.8)                          -- 80%
    elseif value == 5 then op:setFilterQ(i, 1.0)                          -- full
    elseif value == 6 then op:setFilterQ(i, t)                            -- ascending
    elseif value == 7 then op:setFilterQ(i, 1.0 - t)                     -- descending
    elseif value == 8 then op:setFilterQ(i, (i % 2 == 0) and 1.0 or 0.0) -- evens
    elseif value == 9 then op:setFilterQ(i, (i % 2 == 1) and 1.0 or 0.0) -- odds
    elseif value == 10 then op:setFilterQ(i, math.sin(t * math.pi))       -- sine
    end
  end
  op:loadFilter(self.controls and self.controls.filters and self.controls.filters.currentTap or 0)
end

function MultitapDelay:applyTypeMacro(value)
  local op = self.objects.op
  local n = op:getTapCount()
  local types = {0, 1, 2, 3, 4} -- off, lp, bp, hp, notch
  for i = 0, n - 1 do
    if value == 0 then op:setFilterType(i, 0)     -- all off
    elseif value == 1 then op:setFilterType(i, 1)  -- all LP
    elseif value == 2 then op:setFilterType(i, 2)  -- all BP
    elseif value == 3 then op:setFilterType(i, 3)  -- all HP
    elseif value == 4 then op:setFilterType(i, 4)  -- all notch
    elseif value == 5 then op:setFilterType(i, (i % 2 == 0) and 1 or 0)  -- evens LP
    elseif value == 6 then op:setFilterType(i, (i % 2 == 0) and 2 or 0)  -- evens BP
    elseif value == 7 then op:setFilterType(i, (i % 2 == 0) and 3 or 0)  -- evens HP
    elseif value == 8 then op:setFilterType(i, (i % 2 == 1) and 1 or 0)  -- odds LP
    elseif value == 9 then op:setFilterType(i, (i % 2 == 1) and 2 or 0)  -- odds BP
    elseif value == 10 then op:setFilterType(i, (i % 2 == 1) and 3 or 0) -- odds HP
    elseif value == 11 then op:setFilterType(i, (i % 4) + 1)             -- cyclical LBHN
    elseif value == 12 then -- cluster 2: LP/BP alternating pairs
      op:setFilterType(i, (math.floor(i / 2) % 2 == 0) and 1 or 2)
    elseif value == 13 then -- cluster 2: LP/HP alternating pairs
      op:setFilterType(i, (math.floor(i / 2) % 2 == 0) and 1 or 3)
    elseif value == 14 then -- cluster 4: each group of 4 gets L/B/H/N
      op:setFilterType(i, (math.floor(i / 4) % 4) + 1)
    elseif value == 15 then -- cluster 8: first 8 LP, second 8 HP
      op:setFilterType(i, (i < 8) and 1 or 3)
    end
  end
  op:loadFilter(self.controls and self.controls.filters and self.controls.filters.currentTap or 0)
end

-- Xform: randomization helper
local function randomizeValue(cur, min, max, depth, spread)
  local range = max - min
  local center = spread * (min + max) * 0.5 + (1 - spread) * cur
  local dev = depth * range * 0.5
  local r = math.random() * 2 - 1
  local v = center + r * dev
  return math.max(min, math.min(max, v))
end

local function randomizeInt(cur, min, max, depth, spread)
  return math.floor(randomizeValue(cur, min, max, depth, spread) + 0.5)
end

function MultitapDelay:fireTransform()
  -- All randomization handled in C++ via stored Bias refs
  self.objects.op:fireRandomize()
end

function MultitapDelay:applyRandomize(target, depth, spread)
  local op = self.objects.op
  local n = op:getTapCount()

  local function rndTapLevels()
    for i = 0, n - 1 do op:setTapLevel(i, randomizeValue(op:getTapLevel(i), 0, 1, depth, spread)) end
  end
  local function rndTapPans()
    for i = 0, n - 1 do op:setTapPan(i, randomizeValue(op:getTapPan(i), -1, 1, depth, spread)) end
  end
  local function rndTapPitch()
    for i = 0, n - 1 do op:setTapPitch(i, randomizeInt(op:getTapPitch(i), -24, 24, depth, spread)) end
  end
  local function rndCutoff()
    for i = 0, n - 1 do op:setFilterCutoff(i, randomizeValue(op:getFilterCutoff(i), 20, 10000, depth, spread)) end
  end
  local function rndQ()
    for i = 0, n - 1 do op:setFilterQ(i, randomizeValue(op:getFilterQ(i), 0, 1, depth, spread)) end
  end
  local function rndType()
    for i = 0, n - 1 do op:setFilterType(i, randomizeInt(op:getFilterType(i), 0, 4, depth, spread)) end
  end
  local function rndParam(obj, name, min, max)
    local cur = obj:getParameter("Bias"):target()
    obj:hardSet("Bias", randomizeValue(cur, min, max, depth, spread))
  end

  if target == 0 then -- all
    rndTapLevels(); rndTapPans(); rndTapPitch()
    rndCutoff(); rndQ(); rndType()
    rndParam(self.objects.masterTime, "Bias", 0.01, 2.0)
    rndParam(self.objects.feedback, "Bias", 0, 0.95)
    rndParam(self.objects.feedbackTone, "Bias", -1, 1)
    rndParam(self.objects.skew, "Bias", -2, 2)
    rndParam(self.objects.grainSize, "Bias", 0, 1)
    rndParam(self.objects.tapCount, "Bias", 1, 16)
  elseif target == 1 then rndTapLevels(); rndTapPans(); rndTapPitch()  -- taps
  elseif target == 2 then -- delay
    rndParam(self.objects.masterTime, "Bias", 0.01, 2.0)
    rndParam(self.objects.feedback, "Bias", 0, 0.95)
    rndParam(self.objects.feedbackTone, "Bias", -1, 1)
    rndParam(self.objects.skew, "Bias", -2, 2)
    rndParam(self.objects.grainSize, "Bias", 0, 1)
    rndParam(self.objects.tapCount, "Bias", 1, 16)
  elseif target == 3 then rndCutoff(); rndQ(); rndType()                -- filters
  elseif target == 4 then rndTapLevels()                                -- level
  elseif target == 5 then rndTapPans()                                  -- pan
  elseif target == 6 then rndTapPitch()                                 -- pitch
  elseif target == 7 then rndCutoff()                                   -- cutoff
  elseif target == 8 then rndQ()                                        -- Q
  elseif target == 9 then rndType()                                     -- type
  elseif target == 10 then rndParam(self.objects.masterTime, "Bias", 0.01, 2.0) -- time
  elseif target == 11 then rndParam(self.objects.feedback, "Bias", 0, 0.95)     -- fdbk
  elseif target == 12 then rndParam(self.objects.feedbackTone, "Bias", -1, 1)   -- tone
  elseif target == 13 then rndParam(self.objects.skew, "Bias", -2, 2)           -- skew
  elseif target == 14 then rndParam(self.objects.grainSize, "Bias", 0, 1)       -- grain
  elseif target == 15 then rndParam(self.objects.tapCount, "Bias", 1, 16)       -- count
  elseif target == 16 then -- reset
    for i = 0, 15 do
      op:setTapLevel(i, 1.0); op:setTapPan(i, 0.0); op:setTapPitch(i, 0)
      op:setFilterCutoff(i, 10000); op:setFilterQ(i, 0.0); op:setFilterType(i, 0)
    end
    self.objects.masterTime:hardSet("Bias", 0.5)
    self.objects.feedback:hardSet("Bias", 0.3)
    self.objects.feedbackTone:hardSet("Bias", 0.0)
    self.objects.skew:hardSet("Bias", 0.0)
    self.objects.grainSize:hardSet("Bias", 0.5)
    self.objects.tapCount:hardSet("Bias", 4)
  end

  -- Refresh edit buffers
  op:loadTap(self.controls and self.controls.taps and self.controls.taps.currentTap or 0)
  op:loadFilter(self.controls and self.controls.filters and self.controls.filters.currentTap or 0)
end

function MultitapDelay:onLoadViews()
  return {
    tune = Pitch {
      button = "V/oct",
      branch = self.branches.tune,
      description = "V/oct",
      offset = self.objects.tune,
      range = self.objects.tuneRange
    },
    taps = TapListControl {
      description = "Taps",
      width = app.SECTION_PLY,
      delay = self.objects.op
    },
    filters = FilterListControl {
      description = "Filters",
      width = app.SECTION_PLY,
      delay = self.objects.op
    },
    overview = RaindropControl {
      delay = self.objects.op,
      width = app.SECTION_PLY * 2
    },
    masterTime = TimeControl {
      button = "time",
      description = "Master Time",
      branch = self.branches.masterTime,
      gainbias = self.objects.masterTime,
      range = self.objects.masterTime,
      biasMap = timeMap,
      biasUnits = app.unitSecs,
      biasPrecision = 2,
      initialBias = 0.5,
      grainSize = self.objects.grainSize:getParameter("Bias"),
      skew = self.objects.skew:getParameter("Bias"),
      tapCount = self.objects.tapCount:getParameter("Bias")
    },
    feedback = FeedbackControl {
      button = "fdbk",
      description = "Feedback",
      branch = self.branches.feedback,
      gainbias = self.objects.feedback,
      range = self.objects.feedback,
      biasMap = feedbackMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.3,
      feedbackTone = self.objects.feedbackTone:getParameter("Bias")
    },
    feedbackTone = GainBias {
      button = "tone",
      description = "Feedback Tone",
      branch = self.branches.feedbackTone,
      gainbias = self.objects.feedbackTone,
      range = self.objects.feedbackTone,
      biasMap = feedbackToneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    mix = MixControl {
      button = "mix",
      description = "Mix",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = mixMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5,
      inputLevel = self.objects.inputLevel:getParameter("Bias"),
      outputLevel = self.objects.outputLevel:getParameter("Bias"),
      tanhAmt = self.objects.tanhAmt:getParameter("Bias")
    },
    inputLevel = GainBias {
      button = "input",
      description = "Input Level",
      branch = self.branches.inputLevel,
      gainbias = self.objects.inputLevel,
      range = self.objects.inputLevel,
      biasMap = inputLevelMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    outputLevel = GainBias {
      button = "out",
      description = "Output Level",
      branch = self.branches.outputLevel,
      gainbias = self.objects.outputLevel,
      range = self.objects.outputLevel,
      biasMap = outputLevelMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    tanhAmt = GainBias {
      button = "tanh",
      description = "Saturation",
      branch = self.branches.tanhAmt,
      gainbias = self.objects.tanhAmt,
      range = self.objects.tanhAmt,
      biasMap = tanhMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
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
    },
    grainSize = GainBias {
      button = "grain",
      description = "Grain Size",
      branch = self.branches.grainSize,
      gainbias = self.objects.grainSize,
      range = self.objects.grainSize,
      biasMap = grainSizeMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    volMacro = MacroControl {
      button = "vol",
      description = "Volume Macro",
      branch = self.branches.volMacro,
      gainbias = self.objects.volMacro,
      range = self.objects.volMacro,
      biasMap = intMap(0, 10),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = volMacroNames,
      applyPreset = function(v) self:applyVolumeMacro(v) end
    },
    panMacro = MacroControl {
      button = "pan",
      description = "Pan Macro",
      branch = self.branches.panMacro,
      gainbias = self.objects.panMacro,
      range = self.objects.panMacro,
      biasMap = intMap(0, 10),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = panMacroNames,
      applyPreset = function(v) self:applyPanMacro(v) end
    },
    cutoffMacro = MacroControl {
      button = "cut",
      description = "Cutoff Macro",
      branch = self.branches.cutoffMacro,
      gainbias = self.objects.cutoffMacro,
      range = self.objects.cutoffMacro,
      biasMap = intMap(0, 5),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = cutoffMacroNames,
      applyPreset = function(v) self:applyCutoffMacro(v) end
    },
    qMacro = MacroControl {
      button = "Q",
      description = "Q Macro",
      branch = self.branches.qMacro,
      gainbias = self.objects.qMacro,
      range = self.objects.qMacro,
      biasMap = intMap(0, 10),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = qMacroNames,
      applyPreset = function(v) self:applyQMacro(v) end
    },
    typeMacro = MacroControl {
      button = "type",
      description = "Type Macro",
      branch = self.branches.typeMacro,
      gainbias = self.objects.typeMacro,
      range = self.objects.typeMacro,
      biasMap = intMap(0, 15),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = typeMacroNames,
      applyPreset = function(v) self:applyTypeMacro(v) end
    },
    xform = TransformGateControl {
      seq = self,
      button = "xform",
      description = "Randomize",
      branch = self.branches.xformGate,
      comparator = self.objects.xformGate,
      funcNames = xformTargetNames,
      funcMap = intMap(0, 16),
      funcParam = self.objects.xformTarget:getParameter("Bias"),
      paramALabel = "depth",
      factorParam = self.objects.xformDepth:getParameter("Bias"),
      factorMap = floatMap(0, 1),
      factorPrecision = 2,
      paramBParam = self.objects.xformSpread:getParameter("Bias"),
      paramBLabel = "sprd",
      paramBMap = floatMap(0, 1),
      paramBPrecision = 2
    }
  }, {
    expanded = { "tune", "taps", "overview", "masterTime", "feedback", "xform", "mix" },
    collapsed = {},
    taps = { "taps", "filters", "tapCount", "volMacro", "panMacro", "cutoffMacro", "qMacro", "typeMacro" },
    masterTime = { "masterTime", "grainSize", "skew", "tapCount" },
    feedback = { "feedback", "feedbackTone" },
    mix = { "mix", "inputLevel", "outputLevel", "tanhAmt" }
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
      pitch = op:getTapPitch(i),
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
        op:setTapPitch(i, tap.pitch or 0.0)
        op:setFilterCutoff(i, tap.filterCutoff or 10000.0)
        op:setFilterQ(i, tap.filterQ or 0.0)
        op:setFilterType(i, tap.filterType or 0)
      end
    end
  end
  self.objects.op:loadTap(0)
  self.objects.op:loadFilter(0)
end

return MultitapDelay
