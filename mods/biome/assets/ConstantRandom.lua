local app = app
local libbiome = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local ConstantRandom = Class {}
ConstantRandom:include(Unit)

function ConstantRandom:init(args)
  args.title = "Constant Random"
  args.mnemonic = "CR"
  Unit.init(self, args)
end

function ConstantRandom:onLoadGraph(channelCount)
  local op = self:addObject("op", libbiome.ConstantRandom())

  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  local rate = self:addObject("rate", app.ParameterAdapter())
  rate:hardSet("Bias", 5.0)
  tie(op, "Rate", rate, "Out")
  self:addMonoBranch("rate", rate, "In", rate, "Out")

  local slew = self:addObject("slew", app.ParameterAdapter())
  slew:hardSet("Bias", 0.0)
  tie(op, "Time", slew, "Out")
  self:addMonoBranch("slew", slew, "In", slew, "Out")

  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Bias", 0.5)
  tie(op, "Level", level, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")
end

local views = {
  expanded = { "rate", "slew", "level" },
  collapsed = {}
}

-- The built-in slewTimes map (timeOctaveMap 0.003..1000) cannot reach 0, so the
-- fastest it offers is 3 ms of glide - there is no true sample-and-hold on it.
-- The framework solves exactly this for FREQUENCY with octaveMapWithZero, which
-- allocates one extra LUT slot and prepends 0 before the octave series; it just
-- never shipped the time equivalent. This is that, built the same way, so 0 ms
-- (a hard jump, pure S&H) sits at the bottom of the throw.
local slewMapWithZero = (function()
  local low, high = 0.003, 1000
  local n, x = 0, low
  while x < high do n = n + 1; x = x * 2 end
  local map = app.LUTDialMap(n + 1)
  map:add(0)
  x = low
  while x < high do map:add(x); x = x * 2 end
  return map
end)()

-- Rate bottoms out at exactly 0 Hz = paused (the DSP holds its last value
-- there). Steps are the framework's own [0,10] pattern one decade up:
-- coarse 0.1 Hz, with superCoarse/fine/superFine extrapolated at the 10x
-- ratio the built-in maps use. The old map was linear 0.01..100 stepping 1 Hz
-- coarse, which put the entire sub-1 Hz range inside a single detent.
local rateMap = (function()
  local m = app.LinearDialMap(0, 100)
  m:setSteps(1, 0.1, 0.01, 0.001)
  return m
end)()

function ConstantRandom:onLoadViews(objects, branches)
  local controls = {}

  controls.rate = GainBias {
    button = "rate",
    description = "Rate",
    branch = branches.rate,
    gainbias = objects.rate,
    range = objects.rate,
    biasMap = rateMap,
    biasUnits = app.unitHertz,
    -- 3 decimals so the extrapolated fine/superFine steps (0.01 / 0.001 Hz)
    -- are actually visible; the slow end is exactly where that resolution
    -- earns its place.
    biasPrecision = 3,
    initialBias = 5.0
  }

  -- The built-in slew time control, taken wholesale: slewTimes octave map
  -- (3 ms .. 1000 s), seconds, octave scaling, standard gain map. Replaces a
  -- private 0-1 "amount" whose middle half only spanned 22..39 ms.
  controls.slew = GainBias {
    button = "slew",
    description = "Slew Time",
    branch = branches.slew,
    gainbias = objects.slew,
    range = objects.slew,
    biasMap = slewMapWithZero,
    biasUnits = app.unitSecs,
    initialBias = 0.0,
    scaling = app.octaveScaling,
    gainMap = Encoder.getMap("gain")
  }

  controls.level = GainBias {
    button = "level",
    description = "Level",
    branch = branches.level,
    gainbias = objects.level,
    range = objects.level,
    -- The built-in oscillator Level convention, taken as-is: every core
    -- oscillator (Sine, SingleCycle, AliasingTriangle, AliasingSaw) uses the
    -- [-1,1] map at a 0.5 default. 0.5 puts the output at +/-5 V, which is what
    -- a modulation source is normally expected to swing; 1.0 (+/-10 V) is still
    -- reachable, and the bipolar map additionally allows a NEGATIVE level =
    -- inverted random, which the old private 0-1 map forbade.
    biasMap = Encoder.getMap("[-1,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }

  return controls, views
end

return ConstantRandom
