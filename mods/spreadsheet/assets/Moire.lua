local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

-- Spread (r): the moving-lattice detune, 0..2.
local spreadMap = (function()
  local m = app.LinearDialMap(0, 2)
  m:setSteps(0.5, 0.1, 0.01, 0.001)
  return m
end)()

local unitMap = (function()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end)()

local Moire = Class {}
Moire:include(Unit)

function Moire:init(args)
  args.title = "Moire"
  args.mnemonic = "Mo"
  Unit.init(self, args)
end

function Moire:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Moire())

  -- Generator unit: sink the chain input (framework expects a sink).
  local sink = self:addObject("sink", app.ConstantGain())
  sink:hardSet("Gain", 0.0)
  connect(self, "In1", sink, "In")

  -- V/Oct (10x scaling, Mirror/Plaits convention -> C++ does 2^voct).
  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  local voctGain = self:addObject("voctGain", app.ConstantGain())
  voctGain:hardSet("Gain", 10.0)
  connect(tune, "Out", voctGain, "In")
  connect(voctGain, "Out", op, "V/Oct")
  connect(tune, "Out", tuneRange, "In")
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  -- Fundamental (base pitch Hz).
  local f0 = self:addObject("f0", app.ParameterAdapter())
  f0:hardSet("Bias", 110.0)
  tie(op, "Fundamental", f0, "Out")
  self:addMonoBranch("f0", f0, "In", f0, "Out")

  -- Audio-rate modulatable inlets (Vitrail addFader pattern).
  local function addFader(name, inletName, defaultBias)
    local o = self:addObject(name, app.GainBias())
    o:hardSet("Gain", 1.0)
    o:hardSet("Bias", defaultBias)
    local rng = self:addObject(name .. "Range", app.MinMax())
    connect(o, "Out", rng, "In")
    connect(o, "Out", op, inletName)
    self:addMonoBranch(name, o, "In", o, "Out")
  end
  addFader("spread", "Spread", 0.0)
  addFader("body", "Body", 0.6)
  addFader("air", "Air", 0.4)
  addFader("couple", "Couple", 0.0)
  addFader("drift", "Drift", 0.2)
  addFader("lock", "Lock", 0.0)

  -- Level.
  local level = self:addObject("level", app.ParameterAdapter())
  level:hardSet("Bias", 0.5)
  tie(op, "Level", level, "Out")
  self:addMonoBranch("level", level, "In", level, "Out")

  connect(op, "Out", self, "Out1")
end

function Moire:onLoadViews()
  local function fader(button, description, name, biasMap, initial)
    return GainBias {
      button = button,
      description = description,
      branch = self.branches[name],
      gainbias = self.objects[name],
      range = self.objects[name .. "Range"],
      biasMap = biasMap,
      biasPrecision = 2,
      initialBias = initial
    }
  end
  return {
    tune = Pitch {
      button = "V/oct",
      branch = self.branches.tune,
      description = "V/oct",
      offset = self.objects.tune,
      range = self.objects.tuneRange
    },
    f0 = GainBias {
      button = "freq",
      description = "Fundamental",
      branch = self.branches.f0,
      gainbias = self.objects.f0,
      range = self.objects.f0,
      biasMap = Encoder.getMap("oscFreq"),
      biasUnits = app.unitHertz,
      biasPrecision = 1,
      initialBias = 110.0
    },
    spread = fader("spread", "Spread (r)", "spread", spreadMap, 0.0),
    body = fader("body", "Body (Q)", "body", unitMap, 0.6),
    air = fader("air", "Air (excite)", "air", unitMap, 0.4),
    couple = fader("couple", "Couple (network)", "couple", unitMap, 0.0),
    drift = fader("drift", "Drift", "drift", unitMap, 0.2),
    lock = fader("lock", "Lock (crystalline)", "lock", unitMap, 0.0),
    level = GainBias {
      button = "level",
      description = "Level",
      branch = self.branches.level,
      gainbias = self.objects.level,
      range = self.objects.level,
      biasMap = unitMap,
      biasPrecision = 2,
      initialBias = 0.5
    }
  }, {
    expanded = { "tune", "f0", "spread", "body", "air", "couple", "drift", "lock", "level" },
    collapsed = {}
  }
end

return Moire
