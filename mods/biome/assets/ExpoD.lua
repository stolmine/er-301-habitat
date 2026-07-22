-- Expo D - trigger-fired decay-only envelope. Trigger jumps to peak, then a
-- shaped decay to 0. The Curve control morphs the decay contour linear <->
-- exponential (bipolar: concave/log on one side, convex/expo on the other).
local app = app
local libbiome = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local Gate = require "Unit.ViewControl.Gate"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local ExpoD = Class {}
ExpoD:include(Unit)

function ExpoD:init(args)
  args.title = "Expo D"
  args.mnemonic = "ED"
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

function ExpoD:onLoadGraph(channelCount)
  local op = self:addObject("op", libbiome.ExpoD())
  local trig = self:addObject("trig", app.Comparator())
  trig:setGateMode()

  connect(trig, "Out", op, "Trigger")
  addFader(self, op, "decay", "Decay", 0.2)
  addFader(self, op, "curve", "Curve", 0.0)
  addFader(self, op, "level", "Level", 1.0)

  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  self:addMonoBranch("trig", trig, "In", trig, "Out")
end

function ExpoD:onLoadViews()
  return {
    trig = Gate {
      button = "trig",
      description = "Trigger",
      branch = self.branches.trig,
      comparator = self.objects.trig
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
    curve = GainBias {
      button = "curve",
      description = "Curve",
      branch = self.branches.curve,
      gainbias = self.objects.curve,
      range = self.objects.curveRange,
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
    expanded = { "trig", "decay", "curve", "level" },
    collapsed = {}
  }
end

return ExpoD
