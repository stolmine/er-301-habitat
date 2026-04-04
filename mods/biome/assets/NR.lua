local app = app
local libstolmine = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local NRCircle = require "biome.NRCircle"
local Encoder = require "Encoder"

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(2, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local primeMap  = intMap(0, 31)
local maskMap   = intMap(0, 3)
local factorMap = intMap(0, 16)
local lengthMap = intMap(1, 16)

local NR = Class {}
NR:include(Unit)

function NR:init(args)
  args.title = "NR"
  args.mnemonic = "NR"
  Unit.init(self, args)
end

function NR:onLoadGraph(channelCount)
  local op = self:addObject("op", libstolmine.NR())

  -- Clock from chain input (Comparator filters noise from G jacks / hot-unplug)
  local clock = self:addObject("clock", app.Comparator())
  clock:setGateMode()
  connect(self, "In1", clock, "In")
  connect(clock, "Out", op, "Clock")

  -- Reset
  local reset = self:addObject("reset", app.Comparator())
  reset:setMode(app.COMPARATOR_TRIGGER_ON_RISE)
  connect(reset, "Out", op, "Reset")
  self:addMonoBranch("reset", reset, "In", reset, "Out")

  -- Prime
  local prime = self:addObject("prime", app.ParameterAdapter())
  prime:hardSet("Bias", 0)
  tie(op, "Prime", prime, "Out")
  self:addMonoBranch("prime", prime, "In", prime, "Out")

  -- Mask
  local mask = self:addObject("mask", app.ParameterAdapter())
  mask:hardSet("Bias", 0)
  tie(op, "Mask", mask, "Out")
  self:addMonoBranch("mask", mask, "In", mask, "Out")

  -- Factor
  local factor = self:addObject("factor", app.ParameterAdapter())
  factor:hardSet("Bias", 1)
  tie(op, "Factor", factor, "Out")
  self:addMonoBranch("factor", factor, "In", factor, "Out")

  -- Length
  local length = self:addObject("length", app.ParameterAdapter())
  length:hardSet("Bias", 16)
  tie(op, "Length", length, "Out")
  self:addMonoBranch("length", length, "In", length, "Out")

  -- Width
  local width = self:addObject("width", app.ParameterAdapter())
  width:hardSet("Bias", 0.5)
  tie(op, "Width", width, "Out")
  self:addMonoBranch("width", width, "In", width, "Out")

  -- Output
  for i = 1, channelCount do
    connect(op, "Out", self, "Out"..i)
  end
end

function NR:onShowMenu()
  return {}, {}
end

function NR:onLoadViews()
  return {
    circle = NRCircle {
      description = "Numeric Repetitor",
      width       = 2 * app.SECTION_PLY,
      nr          = self.objects.op,
      prime       = self.objects.prime:getParameter("Bias"),
      mask        = self.objects.mask:getParameter("Bias"),
      factor      = self.objects.factor:getParameter("Bias"),
      length      = self.objects.length:getParameter("Bias")
    },
    reset = Gate {
      button      = "reset",
      description = "Reset",
      branch      = self.branches.reset,
      comparator  = self.objects.reset
    },
    prime = GainBias {
      button        = "prime",
      description   = "Prime",
      branch        = self.branches.prime,
      gainbias      = self.objects.prime,
      range         = self.objects.prime,
      biasMap       = primeMap,
      biasUnits     = app.unitNone,
      biasPrecision = 0,
      initialBias   = 0
    },
    mask = GainBias {
      button        = "mask",
      description   = "Mask",
      branch        = self.branches.mask,
      gainbias      = self.objects.mask,
      range         = self.objects.mask,
      biasMap       = maskMap,
      biasUnits     = app.unitNone,
      biasPrecision = 0,
      initialBias   = 0
    },
    factor = GainBias {
      button        = "factor",
      description   = "Factor",
      branch        = self.branches.factor,
      gainbias      = self.objects.factor,
      range         = self.objects.factor,
      biasMap       = factorMap,
      biasUnits     = app.unitNone,
      biasPrecision = 0,
      initialBias   = 1
    },
    length = GainBias {
      button        = "len",
      description   = "Length",
      branch        = self.branches.length,
      gainbias      = self.objects.length,
      range         = self.objects.length,
      biasMap       = lengthMap,
      biasUnits     = app.unitNone,
      biasPrecision = 0,
      initialBias   = 16
    },
    width = GainBias {
      button        = "width",
      description   = "Width",
      branch        = self.branches.width,
      gainbias      = self.objects.width,
      range         = self.objects.width,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    }
  }, {
    expanded  = { "circle" },
    circle    = { "circle", "reset", "prime", "mask", "factor", "length", "width" },
    collapsed = { "circle" }
  }
end

return NR
