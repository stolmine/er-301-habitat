local app = app
local libstolmine = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Encoder = require "Encoder"

local Mordant = Class {}
Mordant:include(Unit)

function Mordant:init(args)
  args.title = "Mordant"
  args.mnemonic = "Md"
  Unit.init(self, args)
end

-- Every mod gain starts at zero: CV is opt-in across the catalog, so a patched
-- inlet stays inert until its gain is raised.
local function adapter(self, name, bias)
  local o = self:addObject(name, app.ParameterAdapter())
  o:hardSet("Gain", 0.0)
  o:hardSet("Bias", bias)
  return o
end

function Mordant:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.Mordant())

  -- Sink the chain input: this is a generator, not a passthrough.
  local sink = self:addObject("sink", app.ConstantGain())
  sink:hardSet("Gain", 0.0)
  connect(self, "In1", sink, "In")

  -- V/Oct
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")

  -- Gate, accent and slide all go through Comparators so they get the
  -- framework's threshold and hysteresis rather than raw chain signal.
  local gate = self:addObject("gate", app.Comparator())
  gate:setGateMode()
  connect(gate, "Out", op, "Gate")

  local accent = self:addObject("accent", app.Comparator())
  accent:setGateMode()
  connect(accent, "Out", op, "Accent")

  local slide = self:addObject("slide", app.Comparator())
  slide:setGateMode()
  connect(slide, "Out", op, "Slide")

  local f0 = adapter(self, "f0", 55.0)
  tie(op, "Fundamental", f0, "Out")

  local cutoff = adapter(self, "cutoff", 500.0)
  tie(op, "Cutoff", cutoff, "Out")

  local reso = adapter(self, "reso", 0.5)
  tie(op, "Resonance", reso, "Out")

  local envmod = adapter(self, "envmod", 0.25)
  tie(op, "Env Mod", envmod, "Out")

  local decay = adapter(self, "decay", 0.4)
  tie(op, "Decay", decay, "Out")

  local accentAmt = adapter(self, "accentAmt", 0.5)
  tie(op, "Accent Amount", accentAmt, "Out")

  local wave = adapter(self, "wave", 0.0)
  tie(op, "Waveform", wave, "Out")

  local level = adapter(self, "level", 0.5)
  tie(op, "Level", level, "Out")

  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  self:addMonoBranch("tune", tune, "In", tune, "Out")
  self:addMonoBranch("gate", gate, "In", gate, "Out")
  self:addMonoBranch("accent", accent, "In", accent, "Out")
  self:addMonoBranch("slide", slide, "In", slide, "Out")
  self:addMonoBranch("f0", f0, "In", f0, "Out")
  self:addMonoBranch("cutoff", cutoff, "In", cutoff, "Out")
  self:addMonoBranch("reso", reso, "In", reso, "Out")
  self:addMonoBranch("envmod", envmod, "In", envmod, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")
  self:addMonoBranch("accentAmt", accentAmt, "In", accentAmt, "Out")
  self:addMonoBranch("wave", wave, "In", wave, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")
end

local views = {
  expanded = {
    "gate", "accent", "slide", "tune", "f0",
    "cutoff", "reso", "envmod", "decay", "accentAmt", "wave", "level"
  },
  collapsed = {}
}

function Mordant:onLoadViews(objects, branches)
  local controls = {}

  controls.gate = Gate {
    button = "gate",
    branch = branches.gate,
    description = "Gate",
    comparator = objects.gate
  }

  controls.accent = Gate {
    button = "accent",
    branch = branches.accent,
    description = "Accent",
    comparator = objects.accent
  }

  controls.slide = Gate {
    button = "slide",
    branch = branches.slide,
    description = "Slide",
    comparator = objects.slide
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
    biasMap = Encoder.getMap("oscFreq"),
    biasUnits = app.unitHertz,
    biasPrecision = 1,
    initialBias = 55.0
  }

  controls.cutoff = GainBias {
    button = "cutoff",
    branch = branches.cutoff,
    description = "Cutoff",
    gainbias = objects.cutoff,
    range = objects.cutoff,
    biasMap = Encoder.getMap("filterFreq"),
    biasUnits = app.unitHertz,
    biasPrecision = 1,
    initialBias = 500.0
  }

  controls.reso = GainBias {
    button = "reso",
    branch = branches.reso,
    description = "Resonance",
    gainbias = objects.reso,
    range = objects.reso,
    biasMap = Encoder.getMap("[0,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.envmod = GainBias {
    button = "envmod",
    branch = branches.envmod,
    description = "Env Mod",
    gainbias = objects.envmod,
    range = objects.envmod,
    biasMap = Encoder.getMap("[0,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.25
  }

  controls.decay = GainBias {
    button = "decay",
    branch = branches.decay,
    description = "Decay",
    gainbias = objects.decay,
    range = objects.decay,
    biasMap = Encoder.getMap("[0,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.4
  }

  controls.accentAmt = GainBias {
    button = "amount",
    branch = branches.accentAmt,
    description = "Accent Amount",
    gainbias = objects.accentAmt,
    range = objects.accentAmt,
    biasMap = Encoder.getMap("[0,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.wave = GainBias {
    button = "wave",
    branch = branches.wave,
    description = "Waveform",
    gainbias = objects.wave,
    range = objects.wave,
    biasMap = Encoder.getMap("[0,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  controls.level = GainBias {
    button = "level",
    branch = branches.level,
    description = "Level",
    gainbias = objects.level,
    range = objects.level,
    biasMap = Encoder.getMap("[0,1]"),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.5
  }

  return controls, views
end

return Mordant
