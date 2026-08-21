-- Gesso -- bus compressor.
--
-- CHARACTER is the colour control, and each position changes FOUR
-- sidechain laws at once rather than scaling one amount:
--
--   glue  feedback detector, RMS-ish, program-dependent, soft knee.
--         Adaptive and gentle; resists deep reduction. SSL-bus territory
--         and the default.
--   peak  the only FEEDFORWARD position: peak detector, fixed timing,
--         hard knee. Catches transients and goes as deep as asked.
--   opto  feedback, RMS, program-dependent with a slow second release
--         stage no fixed Release setting reproduces.
--
-- Measured differences, not claims: at identical settings peak reaches
-- -13.9 dB of reduction where glue reaches -7.0; glue's time constant
-- moves 50% with Ratio while peak's does not move at all; glue and opto
-- speed up 8x and 12x with overshoot while peak speeds up 1.6x.
--
-- Research: planning/compressor-character-research.md
-- DSP: atoms/GlueComp.h

local app = app
local libhouse = require "house.libhouse"
local Encoder = require "Encoder"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Fader = require "Unit.ViewControl.Fader"
local OptionControl = require "Unit.ViewControl.OptionControl"

local function lin(min, max, coarse, fine)
  local m = app.LinearDialMap(min, max)
  m:setSteps(coarse, fine, fine / 10, fine / 100)
  return m
end

local Gesso = Class {}
Gesso:include(Unit)

function Gesso:init(args)
  args.title = "Gesso"
  args.mnemonic = "Gs"
  Unit.init(self, args)
end

function Gesso:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.Gesso())
  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  else
    -- Mono: feed the right channel so the linked detector sees the same
    -- signal, and ignore its output. An unfed inlet reads whatever the
    -- previous unit left in the buffer.
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

  adapter("threshold", "Threshold", 0.5)
  adapter("ratio", "Ratio", 0.0)
  adapter("attack", "Attack", 0.01)
  adapter("release", "Release", 0.2)
  adapter("makeup", "Makeup", 1.0)
  adapter("mix", "Mix", 1.0)
end

function Gesso:onLoadViews()
  local function gb(key, description, map, bias, prec)
    return GainBias {
      button = key, description = description,
      branch = self.branches[key],
      gainbias = self.objects[key], range = self.objects[key],
      biasMap = map, biasUnits = app.unitNone,
      biasPrecision = prec or 2, initialBias = bias
    }
  end
  local function fd(key, description, map, prec)
    return Fader {
      button = key, description = description,
      param = self.objects[key]:getParameter("Bias"),
      map = map, units = app.unitNone, precision = prec or 3
    }
  end

  return {
    thresh = gb("threshold", "Thresh", Encoder.getMap("[0,1]"), 0.5),
    ratio  = gb("ratio",  "Ratio",  Encoder.getMap("[0,1]"), 0.0),
    -- "Char", not "Character": OptionControl renders
    -- "<description>: <choice>" on the ply, split on spaces and sized to
    -- one ply width, and "Character:" is 10 glyphs and overflows.
    char = OptionControl {
      button = "char", description = "Char",
      option = self.objects.op:getOption("Char"),
      choices = { "glue", "peak", "opto" }
    },
    makeup = gb("makeup", "Makeup", lin(0, 8, 1, 0.1), 1.0),
    mix    = gb("mix",    "Mix",    Encoder.getMap("[0,1]"), 1.0),

    attackF  = fd("attack",  "Attack",  lin(0.00002, 0.1, 0.01, 0.001), 4),
    releaseF = fd("release", "Release", lin(0.002, 2.0, 0.2, 0.02))
  }, {
    expanded = { "thresh", "ratio", "char", "makeup", "mix" },
    collapsed = {},
    -- The original control key leads each expansion, or the custom
    -- graphic is replaced by a plain fader.
    ratio = { "ratio", "attackF", "releaseF" }
  }
end

return Gesso
