local app = app
local libclouds = require "clouds.libclouds"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local OptionControl = require "Unit.MenuControl.OptionControl"
local Encoder = require "Encoder"

local modeMap = (function()
  local map = app.LinearDialMap(0, 2)
  map:setSteps(1, 1, 1, 1)
  map:setRounding(1)
  return map
end)()

local pitchMap = (function()
  local map = app.LinearDialMap(-48, 48)
  map:setSteps(1, 1, 0.1, 0.01)
  return map
end)()

local Clouds = Class {}
Clouds:include(Unit)

function Clouds:init(args)
  args.title = "Clouds"
  args.mnemonic = "Cl"
  Unit.init(self, args)
end

function Clouds:onLoadGraph(channelCount)
  local op = self:addObject("op", libclouds.Clouds())

  -- Audio routing
  if channelCount > 1 then
    connect(self, "In1", op, "In L")
    connect(self, "In2", op, "In R")
    connect(op, "Out L", self, "Out1")
    connect(op, "Out R", self, "Out2")
  else
    connect(self, "In1", op, "In L")
    connect(self, "In1", op, "In R")
    connect(op, "Out L", self, "Out1")
  end

  -- Trigger
  local trig = self:addObject("trig", app.Comparator())
  trig:setGateMode()
  connect(trig, "Out", op, "Trigger")
  self:addMonoBranch("trig", trig, "In", trig, "Out")

  -- Freeze
  local freeze = self:addObject("freeze", app.Comparator())
  freeze:setToggleMode()
  connect(freeze, "Out", op, "Freeze")
  self:addMonoBranch("freeze", freeze, "In", freeze, "Out")

  -- Mode
  local mode = self:addObject("mode", app.ParameterAdapter())
  mode:hardSet("Bias", 0)
  tie(op, "Mode", mode, "Out")
  self:addMonoBranch("mode", mode, "In", mode, "Out")

  -- Position
  local position = self:addObject("position", app.ParameterAdapter())
  position:hardSet("Bias", 0.5)
  tie(op, "Position", position, "Out")
  self:addMonoBranch("position", position, "In", position, "Out")

  -- Size
  local size = self:addObject("size", app.ParameterAdapter())
  size:hardSet("Bias", 0.5)
  tie(op, "Size", size, "Out")
  self:addMonoBranch("size", size, "In", size, "Out")

  -- Pitch
  local pitch = self:addObject("pitch", app.ParameterAdapter())
  pitch:hardSet("Bias", 0.0)
  tie(op, "Pitch", pitch, "Out")
  self:addMonoBranch("pitch", pitch, "In", pitch, "Out")

  -- Density
  local density = self:addObject("density", app.ParameterAdapter())
  density:hardSet("Bias", 0.5)
  tie(op, "Density", density, "Out")
  self:addMonoBranch("density", density, "In", density, "Out")

  -- Texture
  local texture = self:addObject("texture", app.ParameterAdapter())
  texture:hardSet("Bias", 0.5)
  tie(op, "Texture", texture, "Out")
  self:addMonoBranch("texture", texture, "In", texture, "Out")

  -- Dry/Wet
  local drywet = self:addObject("drywet", app.ParameterAdapter())
  drywet:hardSet("Bias", 0.5)
  tie(op, "Dry/Wet", drywet, "Out")
  self:addMonoBranch("drywet", drywet, "In", drywet, "Out")

  -- Feedback
  local feedback = self:addObject("feedback", app.ParameterAdapter())
  feedback:hardSet("Bias", 0.0)
  tie(op, "Feedback", feedback, "Out")
  self:addMonoBranch("feedback", feedback, "In", feedback, "Out")

  -- Spread
  local spread = self:addObject("spread", app.ParameterAdapter())
  spread:hardSet("Bias", 0.5)
  tie(op, "Spread", spread, "Out")
  self:addMonoBranch("spread", spread, "In", spread, "Out")
end

function Clouds:onShowMenu(objects)
  return {
    quality = OptionControl {
      description = "Quality",
      option      = objects.op:getOption("Quality"),
      choices     = { "stereo", "mono" },
      boolean     = true
    },
    preamp = OptionControl {
      description = "Preamp",
      option      = objects.op:getOption("Preamp"),
      choices     = { "unity", "x2", "x3" },
      boolean     = true
    }
  }, { "quality", "preamp" }
end

function Clouds:onLoadViews()
  return {
    mode = GainBias {
      button        = "mode",
      description   = "Mode",
      branch        = self.branches.mode,
      gainbias      = self.objects.mode,
      range         = self.objects.mode,
      biasMap       = modeMap,
      biasUnits     = app.unitNone,
      biasPrecision = 0,
      initialBias   = 0
    },
    position = GainBias {
      button        = "pos",
      description   = "Position",
      branch        = self.branches.position,
      gainbias      = self.objects.position,
      range         = self.objects.position,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    size = GainBias {
      button        = "size",
      description   = "Size",
      branch        = self.branches.size,
      gainbias      = self.objects.size,
      range         = self.objects.size,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    density = GainBias {
      button        = "dens",
      description   = "Density",
      branch        = self.branches.density,
      gainbias      = self.objects.density,
      range         = self.objects.density,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    texture = GainBias {
      button        = "text",
      description   = "Texture",
      branch        = self.branches.texture,
      gainbias      = self.objects.texture,
      range         = self.objects.texture,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    pitch = GainBias {
      button        = "pitch",
      description   = "Pitch",
      branch        = self.branches.pitch,
      gainbias      = self.objects.pitch,
      range         = self.objects.pitch,
      biasMap       = pitchMap,
      biasPrecision = 1,
      initialBias   = 0.0
    },
    drywet = GainBias {
      button        = "mix",
      description   = "Dry/Wet",
      branch        = self.branches.drywet,
      gainbias      = self.objects.drywet,
      range         = self.objects.drywet,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    feedback = GainBias {
      button        = "fdbk",
      description   = "Feedback",
      branch        = self.branches.feedback,
      gainbias      = self.objects.feedback,
      range         = self.objects.feedback,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.0
    },
    spread = GainBias {
      button        = "sprd",
      description   = "Spread",
      branch        = self.branches.spread,
      gainbias      = self.objects.spread,
      range         = self.objects.spread,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    freeze = Gate {
      button      = "freez",
      description = "Freeze",
      branch      = self.branches.freeze,
      comparator  = self.objects.freeze
    },
    trig = Gate {
      button      = "trig",
      description = "Trigger",
      branch      = self.branches.trig,
      comparator  = self.objects.trig
    }
  }, {
    expanded  = { "mode", "trig", "freeze", "position", "size", "density",
                  "texture", "pitch", "drywet", "feedback", "spread" },
    collapsed = {}
  }
end

return Clouds
