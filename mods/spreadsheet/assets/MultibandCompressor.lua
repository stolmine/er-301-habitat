local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local CompBandControl = require "spreadsheet.CompBandControl"
local DriveControl = require "spreadsheet.DriveControl"
local CompMixControl = require "spreadsheet.CompMixControl"
local CompSidechainControl = require "spreadsheet.CompSidechainControl"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end

local driveMap = (function()
  local m = app.LinearDialMap(0, 4)
  m:setSteps(0.5, 0.1, 0.01, 0.001)
  return m
end)()

local toneMap = floatMap(0, 1)
local toneFreqMap = (function()
  local m = app.LinearDialMap(20, 20000)
  m:setSteps(1000, 100, 10, 1)
  return m
end)()

local skewMap = floatMap(-1, 1)
local mixMap = floatMap(0, 1)
local outputMap = floatMap(0, 2)
local weightMap = (function()
  local m = app.LinearDialMap(0.1, 4)
  m:setSteps(0.5, 0.1, 0.01, 0.001)
  return m
end)()

local thresholdMap = (function()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end)()

local ratioMap = (function()
  local m = app.LinearDialMap(1, 20)
  m:setSteps(1, 0.5, 0.1, 0.1)
  return m
end)()

local speedMap = floatMap(0, 1)

local attackMap = (function()
  local m = app.LinearDialMap(0.0001, 0.1)
  m:setSteps(0.01, 0.001, 0.0001, 0.0001)
  return m
end)()

local releaseMap = (function()
  local m = app.LinearDialMap(0.001, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end)()

local inputGainMap = (function()
  local m = app.LinearDialMap(0, 4)
  m:setSteps(0.5, 0.1, 0.01, 0.001)
  return m
end)()

local MultibandCompressor = Class {}
MultibandCompressor:include(Unit)

function MultibandCompressor:init(args)
  args.title = "Impasto"
  args.mnemonic = "Im"
  Unit.init(self, args)
end

function MultibandCompressor:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.MultibandCompressor())
  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")

  local stereo = channelCount > 1
  if stereo then
    local opR = self:addObject("opR", libspreadsheet.MultibandCompressor())
    connect(self, "In2", opR, "In")
    connect(opR, "Out", self, "Out2")
  end

  local function tieParam(name, adapter)
    tie(op, name, adapter, "Out")
    if stereo then tie(self.objects.opR, name, adapter, "Out") end
  end

  -- Drive
  local drive = self:addObject("drive", app.ParameterAdapter())
  drive:hardSet("Bias", 1.0)
  tieParam("Drive", drive)
  self:addMonoBranch("drive", drive, "In", drive, "Out")

  local toneAmount = self:addObject("toneAmount", app.ParameterAdapter())
  toneAmount:hardSet("Bias", 0.0)
  tieParam("ToneAmount", toneAmount)
  self:addMonoBranch("toneAmount", toneAmount, "In", toneAmount, "Out")

  local toneFreq = self:addObject("toneFreq", app.ParameterAdapter())
  toneFreq:hardSet("Bias", 800.0)
  tieParam("ToneFreq", toneFreq)
  self:addMonoBranch("toneFreq", toneFreq, "In", toneFreq, "Out")

  local skew = self:addObject("skew", app.ParameterAdapter())
  skew:hardSet("Bias", 0.0)
  tieParam("Skew", skew)
  self:addMonoBranch("skew", skew, "In", skew, "Out")

  -- Input gain (for sidechain control)
  local inputGain = self:addObject("inputGain", app.ParameterAdapter())
  inputGain:hardSet("Bias", 1.0)
  tieParam("InputGain", inputGain)

  -- Sidechain: ConstantGain adapter so the branch has an input and output
  local scGain = self:addObject("scGain", app.ConstantGain())
  scGain:hardSet("Gain", 1.0)
  connect(scGain, "Out", op, "Sidechain")
  if stereo then
    connect(scGain, "Out", self.objects.opR, "Sidechain")
  end
  self:addMonoBranch("sidechain", scGain, "In", scGain, "Out")

  -- Mix
  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 1.0)
  tieParam("Mix", mix)
  self:addMonoBranch("mix", mix, "In", mix, "Out")

  local outputLevel = self:addObject("outputLevel", app.ParameterAdapter())
  outputLevel:hardSet("Bias", 1.0)
  tieParam("OutputLevel", outputLevel)
  self:addMonoBranch("outputLevel", outputLevel, "In", outputLevel, "Out")

  -- Per-band adapters
  for i = 0, 2 do
    local threshold = self:addObject("bandThreshold" .. i, app.ParameterAdapter())
    threshold:hardSet("Bias", 0.5)
    tieParam("BandThreshold" .. i, threshold)
    self:addMonoBranch("bandThreshold" .. i, threshold, "In", threshold, "Out")

    local ratio = self:addObject("bandRatio" .. i, app.ParameterAdapter())
    ratio:hardSet("Bias", 2.0)
    tieParam("BandRatio" .. i, ratio)
    self:addMonoBranch("bandRatio" .. i, ratio, "In", ratio, "Out")

    local speed = self:addObject("bandSpeed" .. i, app.ParameterAdapter())
    speed:hardSet("Bias", 0.3)
    tieParam("BandSpeed" .. i, speed)
    self:addMonoBranch("bandSpeed" .. i, speed, "In", speed, "Out")

    local attack = self:addObject("bandAttack" .. i, app.ParameterAdapter())
    attack:hardSet("Bias", 0.001)
    tieParam("BandAttack" .. i, attack)
    self:addMonoBranch("bandAttack" .. i, attack, "In", attack, "Out")

    local release = self:addObject("bandRelease" .. i, app.ParameterAdapter())
    release:hardSet("Bias", 0.05)
    tieParam("BandRelease" .. i, release)
    self:addMonoBranch("bandRelease" .. i, release, "In", release, "Out")

    local weight = self:addObject("bandWeight" .. i, app.ParameterAdapter())
    weight:hardSet("Bias", 1.0)
    tieParam("BandWeight" .. i, weight)
    self:addMonoBranch("bandWeight" .. i, weight, "In", weight, "Out")

    local bandLevel = self:addObject("bandLevel" .. i, app.ParameterAdapter())
    bandLevel:hardSet("Bias", 1.0)
    tieParam("BandLevel" .. i, bandLevel)
    self:addMonoBranch("bandLevel" .. i, bandLevel, "In", bandLevel, "Out")
  end

  -- Set Bias refs for per-band params
  -- [band][param]: 0=threshold, 1=ratio, 2=speed, 3=attack, 4=release, 5=weight
  for i = 0, 2 do
    op:setBandBias(i, 0, self.objects["bandThreshold" .. i]:getParameter("Bias"))
    op:setBandBias(i, 1, self.objects["bandRatio" .. i]:getParameter("Bias"))
    op:setBandBias(i, 2, self.objects["bandSpeed" .. i]:getParameter("Bias"))
    op:setBandBias(i, 3, self.objects["bandAttack" .. i]:getParameter("Bias"))
    op:setBandBias(i, 4, self.objects["bandRelease" .. i]:getParameter("Bias"))
    op:setBandBias(i, 5, self.objects["bandWeight" .. i]:getParameter("Bias"))
    op:setBandLevelBias(i, self.objects["bandLevel" .. i]:getParameter("Bias"))
    if stereo then
      self.objects.opR:setBandBias(i, 0, self.objects["bandThreshold" .. i]:getParameter("Bias"))
      self.objects.opR:setBandBias(i, 1, self.objects["bandRatio" .. i]:getParameter("Bias"))
      self.objects.opR:setBandBias(i, 2, self.objects["bandSpeed" .. i]:getParameter("Bias"))
      self.objects.opR:setBandBias(i, 3, self.objects["bandAttack" .. i]:getParameter("Bias"))
      self.objects.opR:setBandBias(i, 4, self.objects["bandRelease" .. i]:getParameter("Bias"))
      self.objects.opR:setBandBias(i, 5, self.objects["bandWeight" .. i]:getParameter("Bias"))
      self.objects.opR:setBandLevelBias(i, self.objects["bandLevel" .. i]:getParameter("Bias"))
    end
  end
end

function MultibandCompressor:onLoadViews()
  local bandNames = { "lo", "mid", "hi" }
  local controls = {}
  local views = {
    expanded = { "drive", "sidechain", "lo", "mid", "hi", "skew", "mix" },
    collapsed = {}
  }

  controls.drive = DriveControl {
    button = "drive",
    description = "Drive",
    branch = self.branches.drive,
    gainbias = self.objects.drive,
    range = self.objects.drive,
    biasMap = driveMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0,
    toneAmount = self.objects.toneAmount:getParameter("Bias"),
    toneFreq = self.objects.toneFreq:getParameter("Bias")
  }

  controls.sidechain = CompSidechainControl {
    button = "side",
    description = "Sidechain",
    branch = self.branches.sidechain,
    compressor = self.objects.op,
    inputGainParam = self.objects.inputGain:getParameter("Bias"),
    map = inputGainMap,
    units = app.unitNone
  }

  for i = 0, 2 do
    local name = bandNames[i + 1]
    local bandLevelMap = (function()
      local m = app.LinearDialMap(0, 2)
      m:setSteps(0.1, 0.01, 0.001, 0.001)
      return m
    end)()

    controls[name] = CompBandControl {
      button = name,
      description = name:upper() .. " Band",
      branch = self.branches["bandLevel" .. i],
      gainbias = self.objects["bandLevel" .. i],
      range = self.objects["bandLevel" .. i],
      biasMap = bandLevelMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0,
      compressor = self.objects.op,
      bandIndex = i,
      thresholdParam = self.objects["bandThreshold" .. i]:getParameter("Bias"),
      ratioParam = self.objects["bandRatio" .. i]:getParameter("Bias"),
      speedParam = self.objects["bandSpeed" .. i]:getParameter("Bias")
    }

    -- Expansion views: band + threshold + ratio + attack + release
    views[name] = {
      name,
      name .. "Threshold",
      name .. "Ratio",
      name .. "Attack",
      name .. "Release"
    }

    controls[name .. "Threshold"] = GainBias {
      button = "thresh",
      description = "Threshold",
      branch = self.branches["bandThreshold" .. i],
      gainbias = self.objects["bandThreshold" .. i],
      range = self.objects["bandThreshold" .. i],
      biasMap = thresholdMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    }
    controls[name .. "Ratio"] = GainBias {
      button = "ratio",
      description = "Ratio",
      branch = self.branches["bandRatio" .. i],
      gainbias = self.objects["bandRatio" .. i],
      range = self.objects["bandRatio" .. i],
      biasMap = ratioMap,
      biasUnits = app.unitNone,
      biasPrecision = 1,
      initialBias = 2.0
    }
    controls[name .. "Attack"] = GainBias {
      button = "atk",
      description = "Attack",
      branch = self.branches["bandAttack" .. i],
      gainbias = self.objects["bandAttack" .. i],
      range = self.objects["bandAttack" .. i],
      biasMap = attackMap,
      biasUnits = app.unitSecs,
      biasPrecision = 4,
      initialBias = 0.001
    }
    controls[name .. "Release"] = GainBias {
      button = "rel",
      description = "Release",
      branch = self.branches["bandRelease" .. i],
      gainbias = self.objects["bandRelease" .. i],
      range = self.objects["bandRelease" .. i],
      biasMap = releaseMap,
      biasUnits = app.unitSecs,
      biasPrecision = 3,
      initialBias = 0.05
    }
  end

  controls.skew = GainBias {
    button = "skew",
    description = "Skew",
    branch = self.branches.skew,
    gainbias = self.objects.skew,
    range = self.objects.skew,
    biasMap = skewMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.mix = CompMixControl {
    button = "mix",
    description = "Mix",
    branch = self.branches.mix,
    gainbias = self.objects.mix,
    range = self.objects.mix,
    biasMap = mixMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0,
    compressor = self.objects.op,
    outputLevel = self.objects.outputLevel:getParameter("Bias")
  }

  -- Drive expansion: drive + tone + toneFreq
  views.drive = { "drive", "driveTone", "driveToneFreq" }
  controls.driveTone = GainBias {
    button = "tone",
    description = "Tone",
    branch = self.branches.toneAmount,
    gainbias = self.objects.toneAmount,
    range = self.objects.toneAmount,
    biasMap = toneMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }
  controls.driveToneFreq = GainBias {
    button = "freq",
    description = "Tone Freq",
    branch = self.branches.toneFreq,
    gainbias = self.objects.toneFreq,
    range = self.objects.toneFreq,
    biasMap = toneFreqMap,
    biasUnits = app.unitHertz,
    biasPrecision = 0,
    initialBias = 800.0
  }
  -- Mix expansion: mix + output
  views.mix = { "mix", "mixOutput" }
  controls.mixOutput = GainBias {
    button = "output",
    description = "Output",
    branch = self.branches.outputLevel,
    gainbias = self.objects.outputLevel,
    range = self.objects.outputLevel,
    biasMap = outputMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }

  return controls, views
end

return MultibandCompressor
