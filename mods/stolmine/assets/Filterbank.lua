local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local ModeSelector = require "stolmine.ModeSelector"
local MixControl = require "stolmine.MixControl"
local BandListControl = require "stolmine.BandListControl"
local FilterResponseControl = require "stolmine.FilterResponseControl"
local Encoder = require "Encoder"

local MenuHeader = require "Unit.MenuControl.Header"
local Task = require "Unit.MenuControl.Task"

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(4, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local mixMap = floatMap(0, 1)
local macroQMap = floatMap(0, 1)
local bandCountMap = intMap(2, 16)
local rotateMap = intMap(-16, 16)
local vOctMap = floatMap(-2, 2)
local slewMap = floatMap(0, 5)
local inputLevelMap = floatMap(0, 4)
local outputLevelMap = floatMap(0, 4)
local tanhMap = floatMap(0, 1)

local builtinScaleNames = {
  [0] = "chr", "maj", "min", "h.min",
  "M.pnt", "m.pnt", "whole", "dor",
  "phry", "lyd", "mixo", "loc"
}

local Filterbank = Class {}
Filterbank:include(Unit)

function Filterbank:init(args)
  args.title = "Tomograph"
  args.mnemonic = "Tm"
  Unit.init(self, args)
end

function Filterbank:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.Filterbank())
  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")

  if channelCount > 1 then
    local opR = self:addObject("opR", libstolmine.Filterbank())
    connect(self, "In2", opR, "In")
    connect(opR, "Out", self, "Out2")
  end

  local function tieParam(name, adapter)
    tie(op, name, adapter, "Out")
    if channelCount > 1 then tie(self.objects.opR, name, adapter, "Out") end
  end

  -- Mix
  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 0.5)
  tieParam("Mix", mix)
  self:addMonoBranch("mix", mix, "In", mix, "Out")

  -- Macro Q
  local macroQ = self:addObject("macroQ", app.ParameterAdapter())
  macroQ:hardSet("Bias", 0.5)
  tieParam("MacroQ", macroQ)
  self:addMonoBranch("macroQ", macroQ, "In", macroQ, "Out")

  -- Band count
  local bandCount = self:addObject("bandCount", app.ParameterAdapter())
  bandCount:hardSet("Bias", 8)
  tieParam("BandCount", bandCount)
  self:addMonoBranch("bandCount", bandCount, "In", bandCount, "Out")

  -- Scale
  local scale = self:addObject("scale", app.ParameterAdapter())
  scale:hardSet("Bias", 0)
  tieParam("Scale", scale)
  self:addMonoBranch("scale", scale, "In", scale, "Out")

  -- Load user .scl files into custom scale slots
  self:loadUserScales(op)

  -- Rotate
  local rotate = self:addObject("rotate", app.ParameterAdapter())
  rotate:hardSet("Bias", 0)
  tieParam("Rotate", rotate)
  self:addMonoBranch("rotate", rotate, "In", rotate, "Out")

  -- V/Oct Offset (10x gain so 1V = 1 octave)
  local vOctOffset = self:addObject("vOctOffset", app.ParameterAdapter())
  local vOctGain = self:addObject("vOctGain", app.ConstantGain())
  vOctGain:hardSet("Gain", 10.0)
  vOctOffset:hardSet("Bias", 0.0)
  connect(vOctGain, "Out", vOctOffset, "In")
  tieParam("VOctOffset", vOctOffset)
  self:addMonoBranch("vOctOffset", vOctGain, "In", vOctOffset, "Out")

  -- Slew
  local slew = self:addObject("slew", app.ParameterAdapter())
  slew:hardSet("Bias", 0.0)
  tieParam("Slew", slew)
  self:addMonoBranch("slew", slew, "In", slew, "Out")

  -- Input level
  local inputLevel = self:addObject("inputLevel", app.ParameterAdapter())
  inputLevel:hardSet("Bias", 1.0)
  tieParam("InputLevel", inputLevel)
  self:addMonoBranch("inputLevel", inputLevel, "In", inputLevel, "Out")

  -- Output level
  local outputLevel = self:addObject("outputLevel", app.ParameterAdapter())
  outputLevel:hardSet("Bias", 1.0)
  tieParam("OutputLevel", outputLevel)
  self:addMonoBranch("outputLevel", outputLevel, "In", outputLevel, "Out")

  -- Tanh
  local tanhAmt = self:addObject("tanhAmt", app.ParameterAdapter())
  tanhAmt:hardSet("Bias", 0.0)
  tieParam("TanhAmt", tanhAmt)
  self:addMonoBranch("tanhAmt", tanhAmt, "In", tanhAmt, "Out")
end

function Filterbank:loadUserScales(op)
  self.scaleNames = {}
  for i, name in pairs(builtinScaleNames) do
    self.scaleNames[i] = name
  end
  self.scaleCount = 12

  local ok, Scala = pcall(require, "core.Quantizer.Scala")
  if not ok then return end
  local Path = require "Path"
  local FileSystem = require "Card.FileSystem"
  local root = Path.join(FileSystem.getRoot("front"), "scales")

  local slot = 0
  local files = {}
  local dirOk, iter = pcall(dir, root)
  if not dirOk then return end
  for filename in iter do
    if FileSystem.isType("scala", filename) then
      files[#files + 1] = filename
    end
  end
  table.sort(files)

  for _, filename in ipairs(files) do
    if slot >= 64 then break end
    local fullPath = Path.join(root, filename)
    local data = Scala.load(fullPath)
    if data and data.tunings and #data.tunings > 0 then
      local function loadInto(target)
        target:beginCustomScale(slot)
        for _, cents in ipairs(data.tunings) do
          if cents > 0 and cents <= 1200 then
            target:addCustomDegree(cents)
          end
        end
        target:endCustomScale(slot)
      end
      loadInto(op)
      if self.objects.opR then loadInto(self.objects.opR) end

      -- Truncate filename for fader label (strip .scl, take first 4 chars)
      local label = filename:match("(.+)%.scl$") or filename
      label = label:sub(1, 4):lower()
      self.scaleNames[12 + slot] = label
      slot = slot + 1
    end
  end

  self.scaleCount = 12 + slot
end

function Filterbank:initBands()
  local op = self.objects.op
  local bandCount = op:getBandCount()
  local logMin = math.log(100)
  local logMax = math.log(10000)
  for i = 0, bandCount - 1 do
    local t = i / (bandCount - 1)
    local hz = math.exp(logMin + t * (logMax - logMin))
    op:setBandFreq(i, hz)
    op:setBandGain(i, 1.0)
    op:setBandType(i, 0)
    if self.objects.opR then
      self.objects.opR:setBandFreq(i, hz)
      self.objects.opR:setBandGain(i, 1.0)
      self.objects.opR:setBandType(i, 0)
    end
  end
  self:reloadEditBuffer()
end

function Filterbank:randomizeBands()
  local op = self.objects.op
  local bandCount = op:getBandCount()
  for i = 0, bandCount - 1 do
    local hz = math.exp(math.log(60) + math.random() * (math.log(16000) - math.log(60)))
    op:setBandFreq(i, hz)
    op:setBandGain(i, math.random() * 3)
    if self.objects.opR then
      self.objects.opR:setBandFreq(i, hz)
      self.objects.opR:setBandGain(i, op:getBandGain(i))
    end
  end
  self:reloadEditBuffer()
end

function Filterbank:setAllType(t)
  local op = self.objects.op
  for i = 0, 15 do
    op:setBandType(i, t)
    if self.objects.opR then self.objects.opR:setBandType(i, t) end
  end
  self:reloadEditBuffer()
end

function Filterbank:reloadEditBuffer()
  local op = self.objects.op
  if self.controls and self.controls.bands then
    op:loadBand(self.controls.bands.currentBand or 0)
  else
    op:loadBand(0)
  end
end

function Filterbank:onShowMenu(objects, branches)
  local controls = {}

  controls.bandHeader = MenuHeader { description = "Bands" }
  controls.initBands = Task {
    description = "Init bands (log spacing)",
    task = function() self:initBands() end
  }
  controls.randomize = Task {
    description = "Randomize bands",
    task = function() self:randomizeBands() end
  }
  controls.loadScala = Task {
    description = "Rescan .scl files",
    task = function()
      self:loadUserScales(self.objects.op)
    end
  }

  controls.typeHeader = MenuHeader { description = "Macro Filter Type" }
  controls.allPeak = Task {
    description = "All peaking",
    task = function() self:setAllType(0) end
  }
  controls.allLP = Task {
    description = "All lowpass",
    task = function() self:setAllType(1) end
  }
  controls.allReson = Task {
    description = "All resonator",
    task = function() self:setAllType(2) end
  }

  return controls, {
    "bandHeader",
    "initBands", "randomize", "loadScala",
    "typeHeader",
    "allPeak", "allLP", "allReson"
  }
end

function Filterbank:onLoadViews()
  return {
    bands = BandListControl {
      description = "Bands",
      width = app.SECTION_PLY,
      filterbank = self.objects.op
    },
    overview = FilterResponseControl {
      filterbank = self.objects.op,
      width = 2 * app.SECTION_PLY,
      bandCount = self.objects.bandCount:getParameter("Bias"),
      vOctOffset = self.objects.vOctOffset:getParameter("Bias"),
      slew = self.objects.slew:getParameter("Bias")
    },
    scale = ModeSelector {
      button = "scale",
      description = "Scale",
      branch = self.branches.scale,
      gainbias = self.objects.scale,
      range = self.objects.scale,
      biasMap = intMap(0, self.scaleCount - 1),
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0,
      modeNames = self.scaleNames
    },
    rotate = GainBias {
      button = "rot",
      description = "Rotate",
      branch = self.branches.rotate,
      gainbias = self.objects.rotate,
      range = self.objects.rotate,
      biasMap = rotateMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0
    },
    macroQ = GainBias {
      button = "Q",
      description = "Macro Q",
      branch = self.branches.macroQ,
      gainbias = self.objects.macroQ,
      range = self.objects.macroQ,
      biasMap = macroQMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    mix = MixControl {
      button = "mix",
      description = "Mix",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = mixMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5,
      inputLevel = self.objects.inputLevel:getParameter("Bias"),
      outputLevel = self.objects.outputLevel:getParameter("Bias"),
      tanhAmt = self.objects.tanhAmt:getParameter("Bias")
    },
    -- Expansion views
    bandCount = GainBias {
      button = "bands",
      description = "Band Count",
      branch = self.branches.bandCount,
      gainbias = self.objects.bandCount,
      range = self.objects.bandCount,
      biasMap = bandCountMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 8
    },
    vOctOffset = GainBias {
      button = "V/Oct",
      description = "V/Oct Offset",
      branch = self.branches.vOctOffset,
      gainbias = self.objects.vOctOffset,
      range = self.objects.vOctOffset,
      biasMap = vOctMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    slew = GainBias {
      button = "slew",
      description = "Slew",
      branch = self.branches.slew,
      gainbias = self.objects.slew,
      range = self.objects.slew,
      biasMap = slewMap,
      biasUnits = app.unitSecs,
      biasPrecision = 2,
      initialBias = 0.0
    },
    inputLevel = GainBias {
      button = "input",
      description = "Input Level",
      branch = self.branches.inputLevel,
      gainbias = self.objects.inputLevel,
      range = self.objects.inputLevel,
      biasMap = inputLevelMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    outputLevel = GainBias {
      button = "out",
      description = "Output Level",
      branch = self.branches.outputLevel,
      gainbias = self.objects.outputLevel,
      range = self.objects.outputLevel,
      biasMap = outputLevelMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    },
    tanhAmt = GainBias {
      button = "tanh",
      description = "Saturation",
      branch = self.branches.tanhAmt,
      gainbias = self.objects.tanhAmt,
      range = self.objects.tanhAmt,
      biasMap = tanhMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    }
  }, {
    expanded = { "bands", "overview", "scale", "rotate", "macroQ", "mix" },
    collapsed = {},
    overview = { "overview", "bandCount", "vOctOffset", "slew" },
    mix = { "mix", "inputLevel", "outputLevel", "tanhAmt" }
  }
end

function Filterbank:serialize()
  local t = Unit.serialize(self)
  local op = self.objects.op
  local bands = {}
  for i = 0, 15 do
    bands[tostring(i)] = {
      freq = op:getBandFreq(i),
      gain = op:getBandGain(i),
      filterType = op:getBandType(i)
    }
  end
  t.bands = bands
  t.mix = self.objects.mix:getParameter("Bias"):target()
  t.macroQ = self.objects.macroQ:getParameter("Bias"):target()
  t.bandCount = self.objects.bandCount:getParameter("Bias"):target()
  t.rotate = self.objects.rotate:getParameter("Bias"):target()
  t.vOctOffset = self.objects.vOctOffset:getParameter("Bias"):target()
  t.slew = self.objects.slew:getParameter("Bias"):target()
  t.inputLevel = self.objects.inputLevel:getParameter("Bias"):target()
  t.outputLevel = self.objects.outputLevel:getParameter("Bias"):target()
  t.tanhAmt = self.objects.tanhAmt:getParameter("Bias"):target()
  return t
end

function Filterbank:deserialize(t)
  Unit.deserialize(self, t)
  if t.bands then
    local op = self.objects.op
    for i = 0, 15 do
      local b = t.bands[tostring(i)]
      if b then
        op:setBandFreq(i, b.freq or 440)
        op:setBandGain(i, b.gain or 1.0)
        op:setBandType(i, b.filterType or 0)
        if self.objects.opR then
          self.objects.opR:setBandFreq(i, b.freq or 440)
          self.objects.opR:setBandGain(i, b.gain or 1.0)
          self.objects.opR:setBandType(i, b.filterType or 0)
        end
      end
    end
  end
  local function restoreParam(name, key)
    if t[key] ~= nil then self.objects[name]:hardSet("Bias", t[key]) end
  end
  restoreParam("mix", "mix")
  restoreParam("macroQ", "macroQ")
  restoreParam("bandCount", "bandCount")
  restoreParam("rotate", "rotate")
  restoreParam("vOctOffset", "vOctOffset")
  restoreParam("slew", "slew")
  restoreParam("inputLevel", "inputLevel")
  restoreParam("outputLevel", "outputLevel")
  restoreParam("tanhAmt", "tanhAmt")
  self.objects.op:loadBand(0)
end

return Filterbank
