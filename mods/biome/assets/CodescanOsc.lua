local app = app
local libstolmine = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Encoder = require "Encoder"

local CodescanOsc = Class {}
CodescanOsc:include(Unit)

function CodescanOsc:init(args)
  args.title = "Bletchley Park"
  args.mnemonic = "CO"
  Unit.init(self, args)
end

function CodescanOsc:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.CodescanOsc())

  -- Load our own .so as the wavetable data
  local libPath = app.roots.rear .. "/v0.7/libs/stolmine/libstolmine.so"
  op:loadData(libPath)

  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  local f0 = self:addObject("f0", app.ParameterAdapter())
  local scan = self:addObject("scan", app.ParameterAdapter())
  local sync = self:addObject("sync", app.Comparator())
  sync:setTriggerMode()

  f0:hardSet("Bias", 110.0)
  scan:hardSet("Bias", 0.0)

  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")
  connect(sync, "Out", op, "Sync")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  tie(op, "Fundamental", f0, "Out")
  tie(op, "Scan", scan, "Out")

  self:addMonoBranch("tune", tune, "In", tune, "Out")
  self:addMonoBranch("f0", f0, "In", f0, "Out")
  self:addMonoBranch("scan", scan, "In", scan, "Out")
  self:addMonoBranch("sync", sync, "In", sync, "Out")
end

local views = {
  expanded = { "tune", "scan", "f0", "sync" },
  collapsed = {}
}

local function scanMap()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.0001)
  return m
end

local function f0Map()
  local m = app.LinearDialMap(0.1, 2000)
  m:setSteps(100, 10, 1, 0.1)
  return m
end

function CodescanOsc:onLoadViews(objects, branches)
  local controls = {}

  controls.tune = Pitch {
    button = "V/Oct",
    branch = branches.tune,
    description = "V/Oct",
    offset = objects.tune,
    range = objects.tuneRange
  }

  controls.scan = GainBias {
    button = "scan",
    branch = branches.scan,
    description = "Scan",
    gainbias = objects.scan,
    range = objects.scan,
    biasMap = scanMap(),
    biasUnits = app.unitNone,
    biasPrecision = 3,
    initialBias = 0.0
  }

  controls.f0 = GainBias {
    button = "f0",
    branch = branches.f0,
    description = "Fundamental",
    gainbias = objects.f0,
    range = objects.f0,
    biasMap = f0Map(),
    biasUnits = app.unitHertz,
    biasPrecision = 1,
    initialBias = 110.0
  }

  controls.sync = Gate {
    button = "sync",
    branch = branches.sync,
    description = "Sync",
    comparator = objects.sync
  }

  return controls, views
end

return CodescanOsc
