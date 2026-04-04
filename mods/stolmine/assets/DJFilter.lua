local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local DJFilter = Class {}
DJFilter:include(Unit)

function DJFilter:init(args)
  args.title = "DJ Filter"
  args.mnemonic = "DJ"
  Unit.init(self, args)
end

function DJFilter:onLoadGraph(channelCount)
  local filt1 = self:addObject("filt1", libstolmine.DJFilter())
  local cut = self:addObject("cut", app.ParameterAdapter())
  local q = self:addObject("q", app.ParameterAdapter())
  cut:hardSet("Bias", 0.0)
  q:hardSet("Bias", 0.5)

  connect(self, "In1", filt1, "In")
  connect(filt1, "Out", self, "Out1")
  tie(filt1, "Cut", cut, "Out")
  tie(filt1, "Q", q, "Out")

  if channelCount > 1 then
    local filt2 = self:addObject("filt2", libstolmine.DJFilter())
    connect(self, "In2", filt2, "In")
    connect(filt2, "Out", self, "Out2")
    tie(filt2, "Cut", cut, "Out")
    tie(filt2, "Q", q, "Out")
  end

  self:addMonoBranch("cut", cut, "In", cut, "Out")
  self:addMonoBranch("q", q, "In", q, "Out")
end

local views = {
  expanded = { "cut", "q" },
  collapsed = {}
}

local function cutMap()
  local m = app.LinearDialMap(-1, 1)
  m:setSteps(0.25, 0.1, 0.01, 0.001)
  return m
end

local function qMap()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.25, 0.1, 0.01, 0.001)
  return m
end

function DJFilter:onLoadViews(objects, branches)
  local controls = {}

  controls.cut = GainBias {
    button = "cut",
    branch = branches.cut,
    description = "Cut",
    gainbias = objects.cut,
    range = objects.cut,
    biasMap = cutMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.q = GainBias {
    button = "Q",
    branch = branches.q,
    description = "Resonance",
    gainbias = objects.q,
    range = objects.q,
    biasMap = qMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }

  return controls, views
end

return DJFilter
