-- WoodenBox -- Airwindows WoodenBox port. Faithful single-instance
-- port; ships under the upstream name per the open-source-attribution
-- rule. Internal-stereo (cross-feedback inside the 4x4 FDN with an
-- intentional L/R swap through the verb -- AW behavior preserved
-- literally per feedback_identical_means_identical).
--
-- 3 parameters: Select (17-stop box character), Reso (feedback),
-- Mix (wet/dry).
-- Design doc: planning/woodenbox-port-plan.md.

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

local WoodenBox = Class {}
WoodenBox:include(Unit)

function WoodenBox:init(args)
  args.title = "WoodenBox"
  args.mnemonic = "Wb"
  Unit.init(self, args)
end

function WoodenBox:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.WoodenBox())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  -- 3 ParameterAdapter ties.
  local select = self:addObject("select", app.ParameterAdapter())
  select:hardSet("Bias", 0.5)
  tie(op, "Select", select, "Out")
  self:addMonoBranch("select", select, "In", select, "Out")

  local reso = self:addObject("reso", app.ParameterAdapter())
  reso:hardSet("Bias", 0.5)
  tie(op, "Reso", reso, "Out")
  self:addMonoBranch("reso", reso, "In", reso, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 0.5)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

function WoodenBox:onLoadViews()
  return {
    select = GainBias {
      button = "box",
      description = "Box (1 of 17)",
      branch = self.branches.select,
      gainbias = self.objects.select,
      range = self.objects.select,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    reso = GainBias {
      button = "reso",
      description = "Resonance",
      branch = self.branches.reso,
      gainbias = self.objects.reso,
      range = self.objects.reso,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    mix = GainBias {
      button = "mix",
      description = "Dry/Wet",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = Encoder.getMap("unit"),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    }
  }, {
    expanded = { "select", "reso", "mix" },
    collapsed = {}
  }
end

return WoodenBox
