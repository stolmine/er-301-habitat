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
local FocusShapeControl = require "spreadsheet.FocusShapeControl"
local ScanSkewControl = require "spreadsheet.ScanSkewControl"
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
local shapeMap  = floatMap(0, 1, {0.1, 0.01, 0.001, 0.0001})
local scanMap   = floatMap(0, 1, {0.1, 0.01, 0.001, 0.0001})
local focusMap  = floatMap(-1, 1, {0.1, 0.01, 0.001, 0.0001})
local skewMap   = floatMap(-1, 1, {0.1, 0.01, 0.001, 0.0001})
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
  adapter("shape0",  "Shape0",  0.0)
  adapter("shape1",  "Shape1",  0.0)
  adapter("shape2",  "Shape2",  0.0)
  adapter("scan",    "Scan",    0.5)
  adapter("focus",   "Focus",   0.0)
  adapter("skew",    "Skew",    0.0)
  adapter("outputLevel", "OutputLevel", 1.0)

  -- Output: just the op's out, duplicated across channels on stereo chain.
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end
end

function Blanda:onLoadViews(objects, branches)
  local controls = {}

  -- Helper that wraps an existing ParameterAdapter (name matches both the
  -- objects key and the branches key, thanks to the adapter() helper in
  -- onLoadGraph) as a stock GainBias ply. Used for the helper plies that
  -- only appear in a parent control's expansion view.
  local function helper(button, description, adapterName, biasMap, precision)
    local obj = objects[adapterName]
    return GainBias {
      button = button,
      description = description,
      branch = branches[adapterName],
      gainbias = obj,
      range = obj,
      biasMap = biasMap,
      biasUnits = app.unitNone,
      biasPrecision = precision or 2,
      initialBias = obj:getParameter("Bias"):value()
    }
  end

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

  -- Enroll the three inputs in the unit's mute group so Solo/Mute work.
  self:addToMuteGroup(controls.in1)
  self:addToMuteGroup(controls.in2)
  self:addToMuteGroup(controls.in3)

  -- Helper plies for in1/in2/in3 expansion: Level / Weight / Offset per input.
  for i = 0, 2 do
    local n = i + 1
    controls["level" .. i]  = helper("lvl",  "Level "  .. n, "level"  .. i, levelMap)
    controls["weight" .. i] = helper("wght", "Weight " .. n, "weight" .. i, weightMap)
    controls["offset" .. i] = helper("ofst", "Offset " .. n, "offset" .. i, offsetMap)
  end

  controls.scan = ScanSkewControl {
    button = "scan",
    description = "Scan",
    branch = branches.scan,
    gainbias = objects.scan,
    range = objects.scan,
    biasMap = scanMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5,
    skewMap = skewMap,
    skewParam = objects.skew:getParameter("Bias")
  }

  -- Scan expansion helper.
  controls.skew = helper("skew", "Skew", "skew", skewMap)

  controls.focus = FocusShapeControl {
    button = "focus",
    description = "Focus",
    branch = branches.focus,
    gainbias = objects.focus,
    range = objects.focus,
    biasMap = focusMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0,
    shapeMap = shapeMap,
    shape0Param = objects.shape0:getParameter("Bias"),
    shape1Param = objects.shape1:getParameter("Bias"),
    shape2Param = objects.shape2:getParameter("Bias")
  }

  -- Focus expansion helpers: one Shape ply per input. Buttons mirror
  -- FocusShapeControl's shift-sub sub-button labels for consistency.
  controls.shape0 = helper("in1", "Shape 1", "shape0", shapeMap)
  controls.shape1 = helper("in2", "Shape 2", "shape1", shapeMap)
  controls.shape2 = helper("in3", "Shape 3", "shape2", shapeMap)

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
    collapsed = {},
    -- Per-control expansion sub-views: pressing expand on any of these
    -- plies surfaces the parent control plus helper plies side-by-side.
    in1 = { "in1", "level0", "weight0", "offset0" },
    in2 = { "in2", "level1", "weight1", "offset1" },
    in3 = { "in3", "level2", "weight2", "offset2" },
    scan = { "scan", "skew" },
    focus = { "focus", "shape0", "shape1", "shape2" }
  }

  return controls, views
end

-- Serialize / deserialize follow the established memory pattern.
local adapterBiases = {
  "level0", "level1", "level2",
  "weight0", "weight1", "weight2",
  "offset0", "offset1", "offset2",
  "shape0", "shape1", "shape2",
  "scan", "focus", "skew", "outputLevel"
}

function Blanda:serialize()
  local t = Unit.serialize(self)
  for _, name in ipairs(adapterBiases) do
    local o = self.objects[name]
    if o then
      t[name] = o:getParameter("Bias"):target()
    end
  end
  -- Solo/mute state per input.
  t.mute = {
    self.controls.in1:isMuted(),
    self.controls.in2:isMuted(),
    self.controls.in3:isMuted()
  }
  t.solo = {
    self.controls.in1:isSolo(),
    self.controls.in2:isSolo(),
    self.controls.in3:isSolo()
  }
  return t
end

function Blanda:deserialize(t)
  Unit.deserialize(self, t)
  for _, name in ipairs(adapterBiases) do
    if t[name] ~= nil and self.objects[name] then
      self.objects[name]:hardSet("Bias", t[name])
    end
  end
  if t.mute then
    local inputs = { self.controls.in1, self.controls.in2, self.controls.in3 }
    for i, c in ipairs(inputs) do
      if t.mute[i] then c:mute() end
    end
  end
  if t.solo then
    local inputs = { self.controls.in1, self.controls.in2, self.controls.in3 }
    for i, c in ipairs(inputs) do
      if t.solo[i] then c:solo() end
    end
  end
end

return Blanda
