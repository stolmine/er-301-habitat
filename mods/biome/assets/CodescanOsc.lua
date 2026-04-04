local app = app
local libstolmine = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
-- local ScanControl = require "biome.ScanControl"
local Task = require "Unit.MenuControl.Task"
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

  -- No default data load on hardware; user loads via menu
  -- On emulator, load our own .so for testing
  if not app.roots or not app.roots.rear then
    op:loadData("testing/linux/libbiome.so")
  end

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
  local vca = self:addObject("vca", app.Multiply())
  local level = self:addObject("level", app.GainBias())
  local levelRange = self:addObject("levelRange", app.MinMax())

  connect(level, "Out", levelRange, "In")
  connect(level, "Out", vca, "Left")
  connect(op, "Out", vca, "Right")
  connect(vca, "Out", self, "Out1")
  if channelCount > 1 then
    connect(vca, "Out", self, "Out2")
  end

  tie(op, "Fundamental", f0, "Out")
  tie(op, "Scan", scan, "Out")

  self:addMonoBranch("scan", scan, "In", scan, "Out")
  self:addMonoBranch("tune", tune, "In", tune, "Out")
  self:addMonoBranch("f0", f0, "In", f0, "Out")
  self:addMonoBranch("sync", sync, "In", sync, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")
end

function CodescanOsc:serialize()
  local t = Unit.serialize(self)
  local path = self.objects.op:getFilePath()
  if path and #path > 0 then
    t.dataFile = path
  end
  return t
end

function CodescanOsc:deserialize(t)
  Unit.deserialize(self, t)
end

function CodescanOsc:doLoadFile()
  local task = function(result)
    if result and result.fullpath then
      self.objects.op:loadData(result.fullpath)
    end
  end
  local FileChooser = require "Card.FileChooser"
  local chooser = FileChooser {
    msg = "Choose Data File",
    goal = "load file",
    pattern = "*",
    history = "codescanOscLoadFile"
  }
  chooser:subscribe("done", task)
  chooser:show()
end

local menu = {
  "loadFile",
  "dataInfo"
}

function CodescanOsc:onShowMenu(objects, branches)
  local controls = {}

  controls.loadFile = Task {
    description = "Load File",
    task = function()
      self:doLoadFile()
    end
  }

  local size = objects.op:getDataSize()
  local path = objects.op:getFilePath()
  local name = path:match("[^/]+$") or "none"
  local info = name .. " (" .. size .. " bytes)"

  controls.dataInfo = Task {
    description = info,
    task = function() end
  }

  return controls, menu
end

local views = {
  expanded = { "scan", "tune", "f0", "sync", "level" },
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

  controls.tune = Pitch {
    button = "V/Oct",
    branch = branches.tune,
    description = "V/Oct",
    offset = objects.tune,
    range = objects.tuneRange
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

  controls.level = GainBias {
    button = "level",
    description = "Level",
    branch = branches.level,
    gainbias = objects.level,
    range = objects.levelRange,
    biasMap = Encoder.getMap("[-1,1]"),
    initialBias = 0.5
  }

  return controls, views
end

return CodescanOsc
