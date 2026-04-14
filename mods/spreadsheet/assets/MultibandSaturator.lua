local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local BandControl = require "spreadsheet.BandControl"
local DriveControl = require "spreadsheet.DriveControl"
local ParfaitMixControl = require "spreadsheet.ParfaitMixControl"
local ModeSelector = require "spreadsheet.ModeSelector"
local ThresholdFader = require "spreadsheet.ThresholdFader"
local Encoder = require "Encoder"

local shaperNames = {
  [0] = "Off",
  [1] = "Tube",
  [2] = "Diode",
  [3] = "Fold",
  [4] = "Half",
  [5] = "Crush",
  [6] = "Sine",
  [7] = "Fractal"
}

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end

local driveMap = (function()
  local map = app.LinearDialMap(0, 16)
  map:setSteps(1, 0.1, 0.01, 0.01)
  return map
end)()

local skewMap = (function()
  local map = app.LinearDialMap(-1, 1)
  map:setSteps(0.25, 0.1, 0.01, 0.01)
  return map
end)()

local mixMap = floatMap(0, 1)
local bandLevelMap = floatMap(0, 2)
local amtMap = floatMap(0, 1)
local biasMap = floatMap(-1, 1)
local weightMap = (function()
  local map = app.LinearDialMap(0.1, 4)
  map:setSteps(0.5, 0.1, 0.01, 0.01)
  return map
end)()
local freqMap = (function()
  local map = app.LinearDialMap(20, 20000)
  map:setSteps(1000, 100, 10, 1)
  return map
end)()
local morphMap = floatMap(0, 1)
local qMap = (function()
  local map = app.LinearDialMap(0.5, 20)
  map:setSteps(1, 0.1, 0.01, 0.01)
  return map
end)()
local typeMap = (function()
  local map = app.LinearDialMap(0, 7)
  map:setSteps(1, 1, 1, 1)
  map:setRounding(1)
  return map
end)()
local toneAmtMap = floatMap(-1, 1)
local toneFreqMap = (function()
  local map = app.LinearDialMap(50, 5000)
  map:setSteps(100, 10, 1, 1)
  return map
end)()
local compMap = floatMap(0, 1)
local outputMap = floatMap(0, 4)
local tanhMap = floatMap(0, 1)
local scHpfMap = (function()
  local map = app.LinearDialMap(0, 1)
  map:setSteps(1, 1, 1, 1)
  map:setRounding(1)
  return map
end)()

local MultibandSaturator = Class {}
MultibandSaturator:include(Unit)

function MultibandSaturator:init(args)
  args.title = "Parfait"
  args.mnemonic = "Pf"
  Unit.init(self, args)
end

function MultibandSaturator:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.MultibandSaturator())

  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    local opR = self:addObject("opR", libspreadsheet.MultibandSaturator())
    connect(self, "In2", opR, "In")
    connect(opR, "Out", self, "Out2")
  end

  local stereo = channelCount > 1

  local function tieParam(name, adapter)
    tie(op, name, adapter, "Out")
    if stereo then tie(self.objects.opR, name, adapter, "Out") end
  end

  -- Drive
  local drive = self:addObject("drive", app.ParameterAdapter())
  drive:hardSet("Bias", 1.0)
  tieParam("Drive", drive)
  self:addMonoBranch("drive", drive, "In", drive, "Out")

  -- Skew
  local skew = self:addObject("skew", app.ParameterAdapter())
  skew:hardSet("Bias", 0.0)
  tieParam("Skew", skew)
  self:addMonoBranch("skew", skew, "In", skew, "Out")

  -- Mix
  local mixParam = self:addObject("mix", app.ParameterAdapter())
  mixParam:hardSet("Bias", 1.0)
  tieParam("Mix", mixParam)
  self:addMonoBranch("mix", mixParam, "In", mixParam, "Out")

  -- Output level
  local outputLevel = self:addObject("outputLevel", app.ParameterAdapter())
  outputLevel:hardSet("Bias", 1.0)
  tieParam("OutputLevel", outputLevel)
  self:addMonoBranch("outputLevel", outputLevel, "In", outputLevel, "Out")

  -- Compress
  local compressAmt = self:addObject("compressAmt", app.ParameterAdapter())
  compressAmt:hardSet("Bias", 0.0)
  tieParam("CompressAmt", compressAmt)
  self:addMonoBranch("compressAmt", compressAmt, "In", compressAmt, "Out")

  -- Tanh
  local tanhAmt = self:addObject("tanhAmt", app.ParameterAdapter())
  tanhAmt:hardSet("Bias", 0.0)
  tieParam("TanhAmt", tanhAmt)
  self:addMonoBranch("tanhAmt", tanhAmt, "In", tanhAmt, "Out")

  -- SC HPF
  local scHpf = self:addObject("scHpf", app.ParameterAdapter())
  scHpf:hardSet("Bias", 0.0)
  tieParam("ScHpf", scHpf)
  self:addMonoBranch("scHpf", scHpf, "In", scHpf, "Out")

  -- Tone
  local toneAmount = self:addObject("toneAmount", app.ParameterAdapter())
  toneAmount:hardSet("Bias", 0.0)
  tieParam("ToneAmount", toneAmount)
  self:addMonoBranch("toneAmount", toneAmount, "In", toneAmount, "Out")

  local toneFreq = self:addObject("toneFreq", app.ParameterAdapter())
  toneFreq:hardSet("Bias", 800.0)
  tieParam("ToneFreq", toneFreq)
  self:addMonoBranch("toneFreq", toneFreq, "In", toneFreq, "Out")

  -- Per-band levels (the band ply main controls)
  for i = 0, 2 do
    local name = "bandLevel" .. i
    local adapter = self:addObject(name, app.ParameterAdapter())
    adapter:hardSet("Bias", 1.0)
    tieParam("BandLevel" .. i, adapter)
    self:addMonoBranch(name, adapter, "In", adapter, "Out")
  end

  -- Per-band weights
  for i = 0, 2 do
    local name = "bandWeight" .. i
    local adapter = self:addObject(name, app.ParameterAdapter())
    adapter:hardSet("Bias", 1.0)
    tieParam("BandWeight" .. i, adapter)
    self:addMonoBranch(name, adapter, "In", adapter, "Out")
  end

  -- Per-band amount
  for i = 0, 2 do
    local name = "bandAmount" .. i
    local adapter = self:addObject(name, app.ParameterAdapter())
    adapter:hardSet("Bias", 0.5)
    tieParam("BandAmount" .. i, adapter)
    self:addMonoBranch(name, adapter, "In", adapter, "Out")
  end

  -- Per-band bias
  for i = 0, 2 do
    local name = "bandBias" .. i
    local adapter = self:addObject(name, app.ParameterAdapter())
    adapter:hardSet("Bias", 0.0)
    tieParam("BandBias" .. i, adapter)
    self:addMonoBranch(name, adapter, "In", adapter, "Out")
  end

  -- Per-band type
  for i = 0, 2 do
    local name = "bandType" .. i
    local adapter = self:addObject(name, app.ParameterAdapter())
    adapter:hardSet("Bias", 0.0)
    tieParam("BandType" .. i, adapter)
    self:addMonoBranch(name, adapter, "In", adapter, "Out")
  end

  -- Per-band filter freq
  for i = 0, 2 do
    local name = "bandFilterFreq" .. i
    local adapter = self:addObject(name, app.ParameterAdapter())
    adapter:hardSet("Bias", 1000.0)
    tieParam("BandFilterFreq" .. i, adapter)
    self:addMonoBranch(name, adapter, "In", adapter, "Out")
  end

  -- Per-band filter morph
  for i = 0, 2 do
    local name = "bandFilterMorph" .. i
    local adapter = self:addObject(name, app.ParameterAdapter())
    adapter:hardSet("Bias", 0.0)
    tieParam("BandFilterMorph" .. i, adapter)
    self:addMonoBranch(name, adapter, "In", adapter, "Out")
  end

  -- Per-band filter Q
  for i = 0, 2 do
    local name = "bandFilterQ" .. i
    local adapter = self:addObject(name, app.ParameterAdapter())
    adapter:hardSet("Bias", 0.5)
    tieParam("BandFilterQ" .. i, adapter)
    self:addMonoBranch(name, adapter, "In", adapter, "Out")
  end

  -- Set band level Bias refs for graphic brightness
  for i = 0, 2 do
    op:setBandLevelBias(i, self.objects["bandLevel" .. i]:getParameter("Bias"))
    if stereo then
      self.objects.opR:setBandLevelBias(i, self.objects["bandLevel" .. i]:getParameter("Bias"))
    end
  end

  -- Set Bias refs for per-band params (direct read from UI, bypasses unscheduled adapters)
  -- param indices: 0=amount, 1=bias, 2=type, 3=weight, 4=filterFreq, 5=filterMorph, 6=filterQ
  for i = 0, 2 do
    op:setBandBias(i, 0, self.objects["bandAmount" .. i]:getParameter("Bias"))
    op:setBandBias(i, 1, self.objects["bandBias" .. i]:getParameter("Bias"))
    op:setBandBias(i, 2, self.objects["bandType" .. i]:getParameter("Bias"))
    op:setBandBias(i, 3, self.objects["bandWeight" .. i]:getParameter("Bias"))
    op:setBandBias(i, 4, self.objects["bandFilterFreq" .. i]:getParameter("Bias"))
    op:setBandBias(i, 5, self.objects["bandFilterMorph" .. i]:getParameter("Bias"))
    op:setBandBias(i, 6, self.objects["bandFilterQ" .. i]:getParameter("Bias"))
    if stereo then
      self.objects.opR:setBandBias(i, 0, self.objects["bandAmount" .. i]:getParameter("Bias"))
      self.objects.opR:setBandBias(i, 1, self.objects["bandBias" .. i]:getParameter("Bias"))
      self.objects.opR:setBandBias(i, 2, self.objects["bandType" .. i]:getParameter("Bias"))
      self.objects.opR:setBandBias(i, 3, self.objects["bandWeight" .. i]:getParameter("Bias"))
      self.objects.opR:setBandBias(i, 4, self.objects["bandFilterFreq" .. i]:getParameter("Bias"))
      self.objects.opR:setBandBias(i, 5, self.objects["bandFilterMorph" .. i]:getParameter("Bias"))
      self.objects.opR:setBandBias(i, 6, self.objects["bandFilterQ" .. i]:getParameter("Bias"))
    end
  end
end

function MultibandSaturator:onLoadViews()
  local bandNames = { "lo", "mid", "hi" }
  local bandDescs = { "Band Low", "Band Mid", "Band High" }

  local controls = {
    drive = DriveControl {
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
    },
    skew = GainBias {
      button = "skew",
      description = "Skew",
      branch = self.branches.skew,
      gainbias = self.objects.skew,
      range = self.objects.skew,
      biasMap = skewMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    },
    mix = ParfaitMixControl {
      button = "mix",
      description = "Mix",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = mixMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0,
      compressAmt = self.objects.compressAmt:getParameter("Bias"),
      outputLevel = self.objects.outputLevel:getParameter("Bias"),
      tanhAmt = self.objects.tanhAmt:getParameter("Bias")
    }
  }

  for i = 0, 2 do
    local bn = bandNames[i + 1]
    controls["band" .. bn] = BandControl {
      button = bn,
      description = bandDescs[i + 1],
      branch = self.branches["bandLevel" .. i],
      gainbias = self.objects["bandLevel" .. i],
      range = self.objects["bandLevel" .. i],
      biasMap = bandLevelMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0,
      amount = self.objects["bandAmount" .. i]:getParameter("Bias"),
      bias = self.objects["bandBias" .. i]:getParameter("Bias"),
      shaperType = self.objects["bandType" .. i]:getParameter("Bias"),
      weight = self.objects["bandWeight" .. i]:getParameter("Bias"),
      filterFreq = self.objects["bandFilterFreq" .. i]:getParameter("Bias"),
      filterMorph = self.objects["bandFilterMorph" .. i]:getParameter("Bias"),
      dspObject = self.objects.op,
      bandIndex = i
    }
    -- Per-band expansion controls
    controls[bn .. "Amt"] = GainBias {
      button = "amt",
      description = bandDescs[i + 1] .. " Amount",
      branch = self.branches["bandAmount" .. i],
      gainbias = self.objects["bandAmount" .. i],
      range = self.objects["bandAmount" .. i],
      biasMap = amtMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    }
    controls[bn .. "Bias"] = GainBias {
      button = "bias",
      description = bandDescs[i + 1] .. " Bias",
      branch = self.branches["bandBias" .. i],
      gainbias = self.objects["bandBias" .. i],
      range = self.objects["bandBias" .. i],
      biasMap = biasMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0
    }
    controls[bn .. "Type"] = ModeSelector {
      button = "type",
      description = bandDescs[i + 1] .. " Type",
      branch = self.branches["bandType" .. i],
      gainbias = self.objects["bandType" .. i],
      range = self.objects["bandType" .. i],
      biasMap = typeMap,
      biasUnits = app.unitNone,
      biasPrecision = 0,
      initialBias = 0.0,
      modeNames = shaperNames
    }
    controls[bn .. "Wt"] = GainBias {
      button = "wt",
      description = bandDescs[i + 1] .. " Weight",
      branch = self.branches["bandWeight" .. i],
      gainbias = self.objects["bandWeight" .. i],
      range = self.objects["bandWeight" .. i],
      biasMap = weightMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    }
    controls[bn .. "Freq"] = GainBias {
      button = "freq",
      description = bandDescs[i + 1] .. " Filter Freq",
      branch = self.branches["bandFilterFreq" .. i],
      gainbias = self.objects["bandFilterFreq" .. i],
      range = self.objects["bandFilterFreq" .. i],
      biasMap = freqMap,
      biasUnits = app.unitHertz,
      biasPrecision = 0,
      initialBias = 1000.0
    }
    controls[bn .. "Morph"] = ThresholdFader {
      button = "morph",
      description = bandDescs[i + 1] .. " Filter Morph",
      branch = self.branches["bandFilterMorph" .. i],
      gainbias = self.objects["bandFilterMorph" .. i],
      range = self.objects["bandFilterMorph" .. i],
      biasMap = morphMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.0,
      thresholdLabels = {
        {0.0, "off"}, {0.01, "LP"}, {0.08, "L>B"}, {0.17, "BP"},
        {0.33, "B>H"}, {0.42, "HP"}, {0.58, "H>N"}, {0.67, "ntch"}
      }
    }
    controls[bn .. "Q"] = GainBias {
      button = "Q",
      description = bandDescs[i + 1] .. " Filter Q",
      branch = self.branches["bandFilterQ" .. i],
      gainbias = self.objects["bandFilterQ" .. i],
      range = self.objects["bandFilterQ" .. i],
      biasMap = qMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    }
  end

  -- Drive expansion controls
  controls.toneAmt = GainBias {
    button = "tone",
    description = "Tone Amount",
    branch = self.branches.toneAmount,
    gainbias = self.objects.toneAmount,
    range = self.objects.toneAmount,
    biasMap = toneAmtMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }
  controls.toneFreq = GainBias {
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

  -- Mix expansion controls
  controls.compAmt = GainBias {
    button = "comp",
    description = "Compress",
    branch = self.branches.compressAmt,
    gainbias = self.objects.compressAmt,
    range = self.objects.compressAmt,
    biasMap = compMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }
  controls.scHpf = GainBias {
    button = "sc hp",
    description = "SC HPF",
    branch = self.branches.scHpf,
    gainbias = self.objects.scHpf,
    range = self.objects.scHpf,
    biasMap = scHpfMap,
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 0.0
  }
  controls.outputLevel = GainBias {
    button = "out",
    description = "Output Level",
    branch = self.branches.outputLevel,
    gainbias = self.objects.outputLevel,
    range = self.objects.outputLevel,
    biasMap = outputMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 1.0
  }
  controls.tanhAmt = GainBias {
    button = "tanh",
    description = "Output Saturation",
    branch = self.branches.tanhAmt,
    gainbias = self.objects.tanhAmt,
    range = self.objects.tanhAmt,
    biasMap = tanhMap,
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  return controls, {
    expanded = { "drive", "bandlo", "bandmid", "bandhi", "skew", "mix" },
    collapsed = {},
    drive = { "drive", "toneAmt", "toneFreq" },
    bandlo = { "bandlo", "loAmt", "loBias", "loType", "loWt", "loFreq", "loMorph", "loQ" },
    bandmid = { "bandmid", "midAmt", "midBias", "midType", "midWt", "midFreq", "midMorph", "midQ" },
    bandhi = { "bandhi", "hiAmt", "hiBias", "hiType", "hiWt", "hiFreq", "hiMorph", "hiQ" },
    mix = { "mix", "compAmt", "scHpf", "outputLevel", "tanhAmt" }
  }
end

-- ParameterAdapter objects with a user-facing Bias. Round-tripped via
-- Excel target/hardSet pattern. BandMute[0..2] on op have
-- enableSerialization() set in C++ constructor for automatic handling.
local adapterBiases = {
  "drive", "skew", "mix", "outputLevel", "compressAmt",
  "tanhAmt", "scHpf", "toneAmount", "toneFreq",
  "bandLevel0", "bandLevel1", "bandLevel2",
  "bandWeight0", "bandWeight1", "bandWeight2",
  "bandAmount0", "bandAmount1", "bandAmount2",
  "bandBias0", "bandBias1", "bandBias2",
  "bandType0", "bandType1", "bandType2",
  "bandFilterFreq0", "bandFilterFreq1", "bandFilterFreq2",
  "bandFilterMorph0", "bandFilterMorph1", "bandFilterMorph2",
  "bandFilterQ0", "bandFilterQ1", "bandFilterQ2"
}

function MultibandSaturator:serialize()
  local t = Unit.serialize(self)
  for _, name in ipairs(adapterBiases) do
    local obj = self.objects[name]
    if obj then
      t[name] = obj:getParameter("Bias"):target()
    end
  end
  return t
end

function MultibandSaturator:deserialize(t)
  Unit.deserialize(self, t)
  for _, name in ipairs(adapterBiases) do
    if t[name] ~= nil and self.objects[name] then
      self.objects[name]:hardSet("Bias", t[name])
    end
  end
end

return MultibandSaturator
