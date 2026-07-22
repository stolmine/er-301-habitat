-- Expo AD - trigger-fired attack-decay envelope. Trigger runs a shaped attack
-- 0 -> peak, then a shaped decay to 0 (fire-and-forget; gate length ignored).
-- Separate Attack Curve and Decay Curve controls morph each segment's contour
-- linear <-> exponential (bipolar).
local app = app
local libbiome = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local Gate = require "Unit.ViewControl.Gate"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local ExpoAD = Class {}
ExpoAD:include(Unit)

function ExpoAD:init(args)
  args.title = "Expo AD"
  args.mnemonic = "EA"
  Unit.init(self, args)
end

-- Audio-rate modulatable fader; mod gain defaults to 0 (CV opt-in).
local function addFader(self, op, name, inletName, defaultBias)
  local o = self:addObject(name, app.GainBias())
  o:hardSet("Gain", 0.0)
  o:hardSet("Bias", defaultBias)
  local r = self:addObject(name .. "Range", app.MinMax())
  connect(o, "Out", r, "In")
  connect(o, "Out", op, inletName)
  self:addMonoBranch(name, o, "In", o, "Out")
  return o, r
end

function ExpoAD:onLoadGraph(channelCount)
  local op = self:addObject("op", libbiome.ExpoAD())
  local trig = self:addObject("trig", app.Comparator())
  trig:setGateMode()

  connect(trig, "Out", op, "Trigger")
  addFader(self, op, "attack", "Attack", 0.01)
  addFader(self, op, "decay", "Decay", 0.2)
  addFader(self, op, "acurve", "Attack Curve", 0.0)
  addFader(self, op, "dcurve", "Decay Curve", 0.0)
  addFader(self, op, "level", "Level", 1.0)

  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  self:addMonoBranch("trig", trig, "In", trig, "Out")
end

function ExpoAD:onLoadViews()
  return {
    trig = Gate {
      button = "trig",
      description = "Trigger",
      branch = self.branches.trig,
      comparator = self.objects.trig
    },
    attack = GainBias {
      button = "attack",
      description = "Attack",
      branch = self.branches.attack,
      gainbias = self.objects.attack,
      range = self.objects.attackRange,
      biasMap = Encoder.getMap("slewTimes"),
      biasUnits = app.unitSecs,
      initialBias = 0.01,
      scaling = app.octaveScaling,
      gainMap = Encoder.getMap("gain")
    },
    decay = GainBias {
      button = "decay",
      description = "Decay",
      branch = self.branches.decay,
      gainbias = self.objects.decay,
      range = self.objects.decayRange,
      biasMap = Encoder.getMap("slewTimes"),
      biasUnits = app.unitSecs,
      initialBias = 0.2,
      scaling = app.octaveScaling,
      gainMap = Encoder.getMap("gain")
    },
    acurve = GainBias {
      button = "acurve",
      description = "Attack Curve",
      branch = self.branches.acurve,
      gainbias = self.objects.acurve,
      range = self.objects.acurveRange,
      biasMap = Encoder.getMap("[-1,1]"),
      biasPrecision = 2,
      initialBias = 0.0
    },
    dcurve = GainBias {
      button = "dcurve",
      description = "Decay Curve",
      branch = self.branches.dcurve,
      gainbias = self.objects.dcurve,
      range = self.objects.dcurveRange,
      biasMap = Encoder.getMap("[-1,1]"),
      biasPrecision = 2,
      initialBias = 0.0
    },
    level = GainBias {
      button = "level",
      description = "Level",
      branch = self.branches.level,
      gainbias = self.objects.level,
      range = self.objects.levelRange,
      biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias = 1.0
    }
  }, {
    expanded = { "trig", "attack", "decay", "acurve", "dcurve", "level" },
    collapsed = {}
  }
end

return ExpoAD
