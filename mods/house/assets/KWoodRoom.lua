-- kWoodRoom -- Airwindows kWoodRoom port. Faithful single-instance
-- port; ships under the upstream name per feedback_no_third_party_branding
-- open-source-attribution rule. Internal-stereo (cross-feedback inside
-- the 6x6) so the C++ atom holds both channels; the Lua wiring just
-- maps In1/In2 -> In L/In R and Out L/Out R -> Out1/Out2.
--
-- 6 parameters surfaced via standard ParameterAdapter + GainBias plies.
-- Design doc: planning/kwoodroom-port-plan.md.

local app = app
local libhouse = require "house.libhouse"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"

local floatMap = function(min, max, precision)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local zeroOneMap = floatMap(0, 1)

local KWoodRoom = Class {}
KWoodRoom:include(Unit)

function KWoodRoom:init(args)
  args.title = "kWoodRoom"
  args.mnemonic = "kW"
  Unit.init(self, args)
end

function KWoodRoom:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.KWoodRoom())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  -- 6 ParameterAdapter ties for the user-facing parameters.
  local regen = self:addObject("regen", app.ParameterAdapter())
  regen:hardSet("Bias", 0.5)
  tie(op, "Regen", regen, "Out")
  self:addMonoBranch("regen", regen, "In", regen, "Out")

  local time = self:addObject("time", app.ParameterAdapter())
  time:hardSet("Bias", 0.5)
  tie(op, "Time", time, "Out")
  self:addMonoBranch("time", time, "In", time, "Out")

  local tone = self:addObject("tone", app.ParameterAdapter())
  tone:hardSet("Bias", 0.25)
  tie(op, "Tone", tone, "Out")
  self:addMonoBranch("tone", tone, "In", tone, "Out")

  local reflect = self:addObject("reflect", app.ParameterAdapter())
  reflect:hardSet("Bias", 0.5)
  tie(op, "Reflect", reflect, "Out")
  self:addMonoBranch("reflect", reflect, "In", reflect, "Out")

  local position = self:addObject("position", app.ParameterAdapter())
  position:hardSet("Bias", 0.75)
  tie(op, "Position", position, "Out")
  self:addMonoBranch("position", position, "In", position, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 0.5)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

function KWoodRoom:onLoadViews()
  return {
    regen = GainBias {
      button = "regen",
      description = "Regeneration",
      branch = self.branches.regen,
      gainbias = self.objects.regen,
      range = self.objects.regen,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    time = GainBias {
      button = "time",
      description = "Time (derez)",
      branch = self.branches.time,
      gainbias = self.objects.time,
      range = self.objects.time,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    tone = GainBias {
      button = "tone",
      description = "Tone (inner Bezier rate)",
      branch = self.branches.tone,
      gainbias = self.objects.tone,
      range = self.objects.tone,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.25
    },
    reflect = GainBias {
      button = "refl",
      description = "Early Reflections",
      branch = self.branches.reflect,
      gainbias = self.objects.reflect,
      range = self.objects.reflect,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    position = GainBias {
      button = "posn",
      description = "Position (delay-set)",
      branch = self.branches.position,
      gainbias = self.objects.position,
      range = self.objects.position,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.75
    },
    mix = GainBias {
      button = "mix",
      description = "Dry/Wet",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    }
  }, {
    expanded = { "regen", "time", "tone", "reflect", "position", "mix" },
    collapsed = {}
  }
end

return KWoodRoom
