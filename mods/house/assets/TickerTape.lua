-- TickerTape -- first chain-as-unit in the house package.
-- Console0Channel (saturate) -> ChromeOxide (tape rot) ->
-- Console0Buss (desaturate) -> ChainMix (dry/wet).
-- 4 plies: Drive / Tape / Bias / Mix.
--
-- Console0 pair gives level-dependent containment around the
-- ChromeOxide tape character. Mix at the end enables parallel
-- processing (dry-blend) for send-style use.
--
-- Per the chain-as-unit pattern (planning/house-atom-architecture.md):
-- atoms are reusable C++ od::Object subclasses; this unit IS the
-- Lua composition of them. Same atoms can ship in future chains
-- (Crush via DeRez2 + Capacitor2, Bloom via Density + Capacitor2,
-- etc.) without C++ changes.
--
-- Plan: planning/tickertape-port-plan.md.

local app = app
local libhouse = require "house.libhouse"
local Encoder = require "Encoder"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"

local floatMap = function(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local zeroOneMap = floatMap(0, 1)

local TickerTape = Class {}
TickerTape:include(Unit)

function TickerTape:init(args)
  args.title = "TickerTape"
  args.mnemonic = "Tt"
  Unit.init(self, args)
end

function TickerTape:onLoadGraph(channelCount)
  -- Signal-flow Objects (the chain atoms).
  local channel = self:addObject("channel", libhouse.Console0Channel())
  local rot     = self:addObject("rot",     libhouse.ChromeOxide())
  local buss    = self:addObject("buss",    libhouse.Console0Buss())
  local mixer   = self:addObject("mixer",   libhouse.ChainMix())

  -- Console pair Pan stays at center (no UI exposure).
  channel:hardSet("Pan", 0.5)
  buss:hardSet("Pan", 0.5)

  -- ----- Wet path: in -> Channel -> ChromeOxide -> Buss -> Mixer Wet -----
  connect(self, "In1", channel, "In L")
  if channelCount > 1 then connect(self, "In2", channel, "In R") end
  connect(channel, "Out L", rot, "In L")
  if channelCount > 1 then connect(channel, "Out R", rot, "In R") end
  connect(rot, "Out L", buss, "In L")
  if channelCount > 1 then connect(rot, "Out R", buss, "In R") end
  connect(buss, "Out L", mixer, "Wet L")
  if channelCount > 1 then connect(buss, "Out R", mixer, "Wet R") end

  -- ----- Dry path: in -> Mixer Dry -----
  connect(self, "In1", mixer, "Dry L")
  if channelCount > 1 then connect(self, "In2", mixer, "Dry R") end

  -- ----- Output from Mixer -----
  connect(mixer, "Out L", self, "Out1")
  if channelCount > 1 then connect(mixer, "Out R", self, "Out2") end

  -- ----- Knob ParameterAdapters (all CV-controllable via plies) -----
  local drive = self:addObject("drive", app.ParameterAdapter())
  drive:hardSet("Bias", 0.5)
  tie(channel, "Gain", drive, "Out")
  tie(buss, "Gain", drive, "Out")  -- symmetric Channel/Bus gain
  self:addMonoBranch("drive", drive, "In", drive, "Out")

  local tape = self:addObject("tape", app.ParameterAdapter())
  tape:hardSet("Bias", 0.5)
  tie(rot, "Drive", tape, "Out")
  self:addMonoBranch("tape", tape, "In", tape, "Out")

  local bias = self:addObject("bias", app.ParameterAdapter())
  bias:hardSet("Bias", 0.3)
  tie(rot, "Output", bias, "Out")
  self:addMonoBranch("bias", bias, "In", bias, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 1.0)  -- default 100% wet
  tie(mixer, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

function TickerTape:onLoadViews()
  return {
    drive = GainBias {
      button = "drive",
      description = "Drive",
      branch = self.branches.drive,
      gainbias = self.objects.drive,
      range = self.objects.drive,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    tape = GainBias {
      button = "tape",
      description = "Tape",
      branch = self.branches.tape,
      gainbias = self.objects.tape,
      range = self.objects.tape,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    bias = GainBias {
      button = "bias",
      description = "Bias",
      branch = self.branches.bias,
      gainbias = self.objects.bias,
      range = self.objects.bias,
      biasMap = zeroOneMap,
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
      biasMap = Encoder.getMap("unit"),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    }
  }, {
    expanded = { "drive", "tape", "bias", "mix" },
    collapsed = {}
  }
end

return TickerTape
