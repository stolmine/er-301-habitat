local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local BandControl = require "spreadsheet.BandControl"
local DriveControl = require "spreadsheet.DriveControl"
local ParfaitMixControl = require "spreadsheet.ParfaitMixControl"
local Encoder = require "Encoder"

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
  local map = app.LinearDialMap(0.1, 4.0)
  map:setSteps(0.5, 0.1, 0.01, 0.01)
  return map
end)()

local mixMap = floatMap(0, 1)
local bandLevelMap = floatMap(0, 2)

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
  skew:hardSet("Bias", 1.0)
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
      initialBias = 1.0
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
    controls["band" .. bandNames[i + 1]] = BandControl {
      button = bandNames[i + 1],
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
  end

  return controls, {
    expanded = { "drive", "bandlo", "bandmid", "bandhi", "skew", "mix" },
    collapsed = {}
  }
end

return MultibandSaturator
