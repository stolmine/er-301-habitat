-- Lacquer -- second chain-as-unit in the house package.
-- Mixed-rate character processor: Cojones (gritty trajectory
-- distortion) inside a downsample shell + TapeFat (clean
-- averaging) inside a 2x upsample bracket. Console0 saturation
-- pair wraps both. Lacquer-cut roughness reconstructed through
-- polished hi-fi playback.
--
-- 4 plies, all continuous with standard coarse/fine encoder
-- stepping (0.1 / 0.01 / 0.001 / 0.001). Cut + Polish drive
-- continuous internal parameters (worldRate 1..8, Cojones
-- disparity scalar 0.5..3.0, TapeFat fatness 3..32, wet blend
-- 0.2..1.0).
--
-- Plan: planning/lacquer-port-plan.md
-- Implementation: atoms/Lacquer.h (monolithic Object — rate
-- brackets don't graph-compose at host rate)

local app = app
local libhouse = require "house.libhouse"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"

local floatMap = function(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end

local zeroOneMap = floatMap(0, 1)

local Lacquer = Class {}
Lacquer:include(Unit)

function Lacquer:init(args)
  args.title = "Lacquer"
  args.mnemonic = "Lq"
  Unit.init(self, args)
end

function Lacquer:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.Lacquer())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  local drive = self:addObject("drive", app.ParameterAdapter())
  drive:hardSet("Bias", 0.5)
  tie(op, "Drive", drive, "Out")
  self:addMonoBranch("drive", drive, "In", drive, "Out")

  local cut = self:addObject("cut", app.ParameterAdapter())
  cut:hardSet("Bias", 0.5)
  tie(op, "Cut", cut, "Out")
  self:addMonoBranch("cut", cut, "In", cut, "Out")

  local polish = self:addObject("polish", app.ParameterAdapter())
  polish:hardSet("Bias", 0.5)
  tie(op, "Polish", polish, "Out")
  self:addMonoBranch("polish", polish, "In", polish, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 1.0)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

function Lacquer:onLoadViews()
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
    cut = GainBias {
      button = "cut",
      description = "Cut",
      branch = self.branches.cut,
      gainbias = self.objects.cut,
      range = self.objects.cut,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    polish = GainBias {
      button = "pol",
      description = "Polish",
      branch = self.branches.polish,
      gainbias = self.objects.polish,
      range = self.objects.polish,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    mix = GainBias {
      button = "mix",
      description = "Mix",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    }
  }, {
    expanded = { "drive", "cut", "polish", "mix" },
    collapsed = {}
  }
end

return Lacquer
