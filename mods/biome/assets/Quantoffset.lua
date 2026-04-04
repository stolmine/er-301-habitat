local app = app
local libcore = require "core.libcore"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local Quantoffset = Class {}
Quantoffset:include(Unit)

function Quantoffset:init(args)
  args.title = "Quantoffset"
  args.mnemonic = "QO"
  Unit.init(self, args)
end

function Quantoffset:onLoadGraph(channelCount)
  local offset = self:addObject("offset", app.GainBias())
  local quantizer = self:addObject("quantizer", libcore.GridQuantizer())
  local levels = self:addObject("levels", app.ParameterAdapter())

  levels:hardSet("Bias", 12)

  connect(offset, "Out", quantizer, "In")
  connect(quantizer, "Out", self, "Out1")
  if channelCount > 1 then
    connect(quantizer, "Out", self, "Out2")
  end

  tie(quantizer, "Levels", levels, "Out")

  self:addMonoBranch("offset", offset, "In", offset, "Out")
  self:addMonoBranch("levels", levels, "In", levels, "Out")
end

local views = {
  expanded = { "offset", "levels" },
  collapsed = {}
}

local function levelsMap()
  local m = app.LinearDialMap(2, 128)
  m:setSteps(12, 1, 1, 1)
  m:setRounding(1)
  return m
end

function Quantoffset:onLoadViews(objects, branches)
  local controls = {}

  controls.offset = GainBias {
    button = "offset",
    branch = branches.offset,
    description = "Offset",
    gainbias = objects.offset,
    range = objects.offset,
    biasMap = Encoder.getMap("default"),
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.0
  }

  controls.levels = GainBias {
    button = "lvls",
    branch = branches.levels,
    description = "Levels",
    gainbias = objects.levels,
    range = objects.levels,
    biasMap = levelsMap(),
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 12
  }

  return controls, views
end

return Quantoffset
