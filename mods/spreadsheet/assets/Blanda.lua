-- Blanda -- three-input scan mixer (working name, Swedish "mingle").
--
-- Pure mixer: three audio inlets (each a MonoBranch the user patches into),
-- a global Scan axis that sweeps through per-input bell response curves,
-- bipolar Focus to scale bell width globally, per-input Weight and Offset.
-- No onboard filtering -- bring your own Canals / Three Sisters / etc.

local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local MixInputControl = require "spreadsheet.MixInputControl"
local Encoder = require "Encoder"

local function floatMap(min, max, steps)
  local m = app.LinearDialMap(min, max)
  if steps then
    m:setSteps(steps[1], steps[2], steps[3], steps[4])
  else
    m:setSteps(0.1, 0.01, 0.001, 0.0001)
  end
  return m
end

local levelMap  = floatMap(0, 2, {0.1, 0.01, 0.001, 0.0001})
local weightMap = floatMap(0, 2, {0.1, 0.01, 0.001, 0.0001})
local offsetMap = floatMap(0, 1, {0.1, 0.01, 0.001, 0.0001})
local scanMap   = floatMap(0, 1, {0.1, 0.01, 0.001, 0.0001})
local focusMap  = floatMap(-1, 1, {0.1, 0.01, 0.001, 0.0001})
local outLevMap = floatMap(0, 2, {0.1, 0.01, 0.001, 0.0001})

local Blanda = Class {}
Blanda:include(Unit)

function Blanda:init(args)
  args.title = "Blanda"
  args.mnemonic = "Bl"
  Unit.init(self, args)
end

function Blanda:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Blanda())

  -- Per-input audio branches via ConstantGain (owns the branch, Gain=1).
  local function audioBranch(idx, inletName, tag)
    local g = self:addObject("gain" .. idx, app.ConstantGain())
    g:setClampInDecibels(-59.9)
    g:hardSet("Gain", 1.0)
    connect(g, "Out", op, inletName)
    self:addMonoBranch(tag, g, "In", g, "Out")
    return g
  end

  audioBranch(0, "In1", "in0")
  audioBranch(1, "In2", "in1")
  audioBranch(2, "In3", "in2")

  -- Per-input shaping params, each an adapter + its own CV branch.
  local function adapter(name, paramName, initial)
    local a = self:addObject(name, app.ParameterAdapter())
    a:hardSet("Bias", initial)
    tie(op, paramName, a, "Out")
    self:addMonoBranch(name, a, "In", a, "Out")
    return a
  end

  adapter("level0",  "Level0",  1.0)
  adapter("level1",  "Level1",  1.0)
  adapter("level2",  "Level2",  1.0)
  adapter("weight0", "Weight0", 1.0)
  adapter("weight1", "Weight1", 1.0)
  adapter("weight2", "Weight2", 1.0)
  adapter("offset0", "Offset0", 0.1667)
  adapter("offset1", "Offset1", 0.5)
  adapter("offset2", "Offset2", 0.8333)
  adapter("scan",    "Scan",    0.5)
  adapter("focus",   "Focus",   0.0)
  adapter("outputLevel", "OutputLevel", 1.0)

  -- Output: just the op's out, duplicated across channels on stereo chain.
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end
end

function Blanda:onLoadViews(objects, branches)
  local controls = {}

  local function input(i, button, description, branchName)
    return MixInputControl {
      button = button,
      description = description,
      inputIndex = i,
      op = objects.op,
      branch = branches[branchName],
      gainbias = objects["level" .. i],
      range = objects["level" .. i],
      biasMap = levelMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      weightMap = weightMap,
      offsetMap = offsetMap,
      weightParam = objects["weight" .. i]:getParameter("Bias"),
      offsetParam = objects["offset" .. i]:getParameter("Bias")
    }
  end

  controls.in1 = input(0, "in1", "Input 1", "in0")
  controls.in2 = input(1, "in2", "Input 2", "in1")
  controls.in3 = input(2, "in3", "Input 3", "in2")

  controls.scan = GainBias {
    button = "scan",
    description = "Scan",
    branch = branches.scan,
    gainbias = objects.scan,
    range = objects.scan,
    biasMap = scanMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.focus = GainBias {
    button = "focus",
    description = "Focus",
    branch = branches.focus,
    gainbias = objects.focus,
    range = objects.focus,
    biasMap = focusMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.level = GainBias {
    button = "lvl",
    description = "Output Level",
    branch = branches.outputLevel,
    gainbias = objects.outputLevel,
    range = objects.outputLevel,
    biasMap = outLevMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }

  local views = {
    expanded = { "in1", "in2", "in3", "scan", "focus", "level" },
    collapsed = {}
  }

  return controls, views
end

-- Serialize / deserialize follow the established memory pattern.
local adapterBiases = {
  "level0", "level1", "level2",
  "weight0", "weight1", "weight2",
  "offset0", "offset1", "offset2",
  "scan", "focus", "outputLevel"
}

function Blanda:serialize()
  local t = Unit.serialize(self)
  for _, name in ipairs(adapterBiases) do
    local o = self.objects[name]
    if o then
      t[name] = o:getParameter("Bias"):target()
    end
  end
  return t
end

function Blanda:deserialize(t)
  Unit.deserialize(self, t)
  for _, name in ipairs(adapterBiases) do
    if t[name] ~= nil and self.objects[name] then
      self.objects[name]:hardSet("Bias", t[name])
    end
  end
end

return Blanda
