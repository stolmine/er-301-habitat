-- Parametric EQ -- four-band, with Q, shelf/bell switching and a
-- level-dependent drive.
--
-- Why this exists next to the firmware's EQ3, measured rather than
-- asserted: asked for +12 dB at its mid, EQ3 gives +9.9 dB at 250 Hz
-- and +9.1 dB at 4 kHz, because it is a three-way crossover splitter
-- with NO Q at all and its frequencies are crossover points, not band
-- centres. This unit at Q=4 lands 12.00 dB and is within 0.4 dB of
-- flat one octave away. EQ3 remains the better choice when you want to
-- CV a gain at audio rate -- its gains are inlets, these are block
-- rate.
--
-- Band layout follows the SSL 611 published spec. Behavioural design
-- informed by specification, NOT a circuit emulation.
--
-- Plan: planning/ochre-character-eq.md
-- DSP: atoms/ParametricEq.h over atoms/ParametricBand.h

local app = app
local libhouse = require "house.libhouse"
local Encoder = require "Encoder"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Fader = require "Unit.ViewControl.Fader"
local OptionControl = require "Unit.ViewControl.OptionControl"
local MenuHeader = require "Unit.MenuControl.Header"
local MenuOption = require "Unit.MenuControl.OptionControl"

-- LinearDialMap, not an octave map: app.OctaveDialMap does not exist
-- in this firmware (checked - nothing in od/ or xroot/ defines it), and
-- requiring a class that is absent fails at INSERT rather than at load,
-- per feedback_verify_framework_class_exists. Steps scale with the
-- range so a narrow band still tunes finely.
local function freqMap(min, max)
  local map = app.LinearDialMap(min, max)
  local span = max - min
  map:setSteps(span / 8, span / 64, span / 512, span / 2048)
  return map
end

local function gainMap()
  -- +/-18 covers Black; Brown clamps to +/-15 in the DSP.
  local map = app.LinearDialMap(-18, 18)
  map:setSteps(6, 1, 0.1, 0.01)
  return map
end

local function qMap()
  local map = app.LinearDialMap(0.3, 10)
  map:setSteps(1, 0.1, 0.01, 0.01)
  return map
end

local ParametricEq = Class {}
ParametricEq:include(Unit)

function ParametricEq:init(args)
  args.title = "Parametric EQ"
  args.mnemonic = "EQ"
  Unit.init(self, args)
end

function ParametricEq:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.ParametricEq())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  else
    -- Mono: feed the right channel too so its band state stays warm
    -- and matched, then ignore its output. Leaving an inlet unfed
    -- would read whatever the previous unit left in the buffer.
    connect(self, "In1", op, "In R")
  end

  local function adapter(name, param, bias)
    local o = self:addObject(name, app.ParameterAdapter())
    o:hardSet("Gain", 0.0)   -- CV is opt-in catalog-wide
    o:hardSet("Bias", bias)
    tie(op, param, o, "Out")
    self:addMonoBranch(name, o, "In", o, "Out")
    return o
  end

  adapter("lfFreq", "LF Freq", 100.0)
  adapter("lfGain", "LF Gain", 0.0)
  adapter("lmfFreq", "LMF Freq", 500.0)
  adapter("lmfGain", "LMF Gain", 0.0)
  adapter("lmfQ", "LMF Q", 1.0)
  adapter("hmfFreq", "HMF Freq", 2000.0)
  adapter("hmfGain", "HMF Gain", 0.0)
  adapter("hmfQ", "HMF Q", 1.0)
  adapter("hfFreq", "HF Freq", 8000.0)
  adapter("hfGain", "HF Gain", 0.0)
  adapter("mix", "Mix", 1.0)
end

function ParametricEq:onLoadMenu(objects, branches)
  return {
    shapeHeader = MenuHeader { description = "Band Shapes" },
    lfShape = MenuOption {
      description = "LF Shape",
      option = objects.op:getOption("LF Shape"),
      choices = { "shelf", "bell" },
      boolean = false
    },
    hfShape = MenuOption {
      description = "HF Shape",
      option = objects.op:getOption("HF Shape"),
      choices = { "shelf", "bell" },
      boolean = false
    }
  }, { "shapeHeader", "lfShape", "hfShape" }
end

function ParametricEq:onLoadViews()
  local function gb(key, description, map, bias, prec)
    return GainBias {
      button        = key,
      description   = description,
      branch        = self.branches[key],
      gainbias      = self.objects[key],
      range         = self.objects[key],
      biasMap       = map,
      biasUnits     = app.unitNone,
      biasPrecision = prec or 2,
      initialBias   = bias
    }
  end
  -- Expansion members use Fader, not GainBias: GainBias requires a
  -- branch and these already have one via the top-level control, so a
  -- second GainBias on the same object would duplicate the CV inlet.
  local function fd(key, description, map, prec)
    return Fader {
      button      = key,
      description = description,
      param       = self.objects[key]:getParameter("Bias"),
      map         = map,
      units       = app.unitNone,
      precision   = prec or 2
    }
  end

  return {
    -- One top-level control per band, showing that band's GAIN, which
    -- is the control reached for most often. Enter expands to the
    -- band's frequency and Q.
    lf   = gb("lfGain",  "LF Gain",  gainMap(), 0.0, 1),
    lmf  = gb("lmfGain", "LMF Gain", gainMap(), 0.0, 1),
    hmf  = gb("hmfGain", "HMF Gain", gainMap(), 0.0, 1),
    hf   = gb("hfGain",  "HF Gain",  gainMap(), 0.0, 1),
    -- Character on a PLY, not buried in the menu. Replaces both the old
    -- Drive fader and the old Colour menu option.
    --
    -- THREE choices, not four: OptionControl has exactly three
    -- sub-buttons, so a fourth is unreachable. A four-position set
    -- shipped once with its last position unselectable.
    character = OptionControl {
      button = "char",
      description = "Character",
      option = self.objects.op:getOption("Character"),
      choices = { "console", "punch", "passive" }
    },
    mix   = gb("mix",   "Mix",   Encoder.getMap("[0,1]"), 1.0),

    lfFreqF  = fd("lfFreq",  "LF Freq",  freqMap(30, 450), 0),
    lmfFreqF = fd("lmfFreq", "LMF Freq", freqMap(200, 2500), 0),
    lmfQF    = fd("lmfQ",    "LMF Q",    qMap()),
    hmfFreqF = fd("hmfFreq", "HMF Freq", freqMap(600, 7000), 0),
    hmfQF    = fd("hmfQ",    "HMF Q",    qMap()),
    hfFreqF  = fd("hfFreq",  "HF Freq",  freqMap(1500, 16000), 0)
  }, {
    expanded  = { "lf", "lmf", "hmf", "hf", "character", "mix" },
    collapsed = {},
    -- The original control key leads each expansion, or the custom
    -- graphic is replaced by a plain fader.
    lf  = { "lf",  "lfFreqF" },
    lmf = { "lmf", "lmfFreqF", "lmfQF" },
    hmf = { "hmf", "hmfFreqF", "hmfQF" },
    hf  = { "hf",  "hfFreqF" }
  }
end

return ParametricEq
