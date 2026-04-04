local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local CodescanFilter = Class {}
CodescanFilter:include(Unit)

function CodescanFilter:init(args)
  args.title = "Station X"
  args.mnemonic = "CF"
  Unit.init(self, args)
end

function CodescanFilter:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.CodescanFilter())

  -- Load our own .so as the FIR kernel data
  local libPath = app.roots.rear .. "/v0.7/libs/stolmine/libstolmine.so"
  op:loadData(libPath)

  local scan = self:addObject("scan", app.ParameterAdapter())
  local taps = self:addObject("taps", app.ParameterAdapter())
  local mix = self:addObject("mix", app.ParameterAdapter())
  scan:hardSet("Bias", 0.0)
  taps:hardSet("Bias", 32)
  mix:hardSet("Bias", 0.5)

  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    local op2 = self:addObject("op2", libstolmine.CodescanFilter())
    op2:loadData(libPath)
    connect(self, "In2", op2, "In")
    connect(op2, "Out", self, "Out2")
    tie(op2, "Scan", scan, "Out")
    tie(op2, "Taps", taps, "Out")
    tie(op2, "Mix", mix, "Out")
  end

  tie(op, "Scan", scan, "Out")
  tie(op, "Taps", taps, "Out")
  tie(op, "Mix", mix, "Out")

  self:addMonoBranch("scan", scan, "In", scan, "Out")
  self:addMonoBranch("taps", taps, "In", taps, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

local views = {
  expanded = { "scan", "taps", "mix" },
  collapsed = {}
}

local function scanMap()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.0001)
  return m
end

local function tapsMap()
  local m = app.LinearDialMap(4, 64)
  m:setSteps(8, 4, 1, 1)
  m:setRounding(1)
  return m
end

local function mixMap()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.25, 0.1, 0.01, 0.001)
  return m
end

function CodescanFilter:onLoadViews(objects, branches)
  local controls = {}

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

  controls.taps = GainBias {
    button = "taps",
    branch = branches.taps,
    description = "Taps",
    gainbias = objects.taps,
    range = objects.taps,
    biasMap = tapsMap(),
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 32
  }

  controls.mix = GainBias {
    button = "mix",
    branch = branches.mix,
    description = "Mix",
    gainbias = objects.mix,
    range = objects.mix,
    biasMap = mixMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }

  return controls, views
end

return CodescanFilter
