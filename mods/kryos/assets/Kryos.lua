local app = app
local libkryos = require "kryos.libkryos"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Encoder = require "Encoder"

local pitchMap = (function()
  local map = app.LinearDialMap(-24, 24)
  map:setSteps(1, 1, 0.1, 0.01)
  return map
end)()

local Kryos = Class {}
Kryos:include(Unit)

function Kryos:init(args)
  args.title = "Kryos"
  args.mnemonic = "Kr"
  Unit.init(self, args)
end

function Kryos:onLoadGraph(channelCount)
  local op = self:addObject("op", libkryos.Kryos())

  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")

  -- Freeze
  local freeze = self:addObject("freeze", app.Comparator())
  freeze:setToggleMode()
  connect(freeze, "Out", op, "Freeze")
  self:addMonoBranch("freeze", freeze, "In", freeze, "Out")

  -- Position
  local position = self:addObject("position", app.ParameterAdapter())
  position:hardSet("Bias", 0.5)
  tie(op, "Position", position, "Out")
  self:addMonoBranch("position", position, "In", position, "Out")

  -- Pitch
  local pitch = self:addObject("pitch", app.ParameterAdapter())
  pitch:hardSet("Bias", 0.0)
  tie(op, "Pitch", pitch, "Out")
  self:addMonoBranch("pitch", pitch, "In", pitch, "Out")

  -- Size
  local size = self:addObject("size", app.ParameterAdapter())
  size:hardSet("Bias", 0.5)
  tie(op, "Size", size, "Out")
  self:addMonoBranch("size", size, "In", size, "Out")

  -- Texture
  local texture = self:addObject("texture", app.ParameterAdapter())
  texture:hardSet("Bias", 0.0)
  tie(op, "Texture", texture, "Out")
  self:addMonoBranch("texture", texture, "In", texture, "Out")

  -- Decay
  local decay = self:addObject("decay", app.ParameterAdapter())
  decay:hardSet("Bias", 0.5)
  tie(op, "Decay", decay, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")

  -- Mix
  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 0.5)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

function Kryos:onLoadViews()
  return {
    freeze = Gate {
      button      = "freez",
      description = "Freeze",
      branch      = self.branches.freeze,
      comparator  = self.objects.freeze
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
    texture = GainBias {
      button        = "text",
      description   = "Texture",
      branch        = self.branches.texture,
      gainbias      = self.objects.texture,
      range         = self.objects.texture,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.0
    },
    decay = GainBias {
      button        = "decay",
      description   = "Decay",
      branch        = self.branches.decay,
      gainbias      = self.objects.decay,
      range         = self.objects.decay,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    mix = GainBias {
      button        = "mix",
      description   = "Mix",
      branch        = self.branches.mix,
      gainbias      = self.objects.mix,
      range         = self.objects.mix,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    }
  }, {
    expanded  = { "freeze", "position", "pitch", "size", "texture", "decay", "mix" },
    collapsed = {}
  }
end

return Kryos
