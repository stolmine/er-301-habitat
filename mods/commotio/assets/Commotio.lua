local app = app
local libcommotio = require "commotio.libcommotio"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Encoder = require "Encoder"

local Commotio = Class {}
Commotio:include(Unit)

function Commotio:init(args)
  args.title = "Commotio"
  args.mnemonic = "Co"
  Unit.init(self, args)
end

function Commotio:onLoadGraph(channelCount)
  local op = self:addObject("op", libcommotio.Commotio())

  connect(op, "Out", self, "Out1")

  -- Gate
  local gate = self:addObject("gate", app.Comparator())
  gate:setGateMode()
  connect(gate, "Out", op, "Gate")
  self:addMonoBranch("gate", gate, "In", gate, "Out")

  -- Bow
  local bowLevel = self:addObject("bowLevel", app.ParameterAdapter())
  bowLevel:hardSet("Bias", 0.0)
  tie(op, "BowLevel", bowLevel, "Out")
  self:addMonoBranch("bowLevel", bowLevel, "In", bowLevel, "Out")

  local bowTimbre = self:addObject("bowTimbre", app.ParameterAdapter())
  bowTimbre:hardSet("Bias", 0.5)
  tie(op, "BowTimbre", bowTimbre, "Out")
  self:addMonoBranch("bowTimbre", bowTimbre, "In", bowTimbre, "Out")

  -- Blow
  local blowLevel = self:addObject("blowLevel", app.ParameterAdapter())
  blowLevel:hardSet("Bias", 0.0)
  tie(op, "BlowLevel", blowLevel, "Out")
  self:addMonoBranch("blowLevel", blowLevel, "In", blowLevel, "Out")

  local blowTimbre = self:addObject("blowTimbre", app.ParameterAdapter())
  blowTimbre:hardSet("Bias", 0.5)
  tie(op, "BlowTimbre", blowTimbre, "Out")
  self:addMonoBranch("blowTimbre", blowTimbre, "In", blowTimbre, "Out")

  local blowMeta = self:addObject("blowMeta", app.ParameterAdapter())
  blowMeta:hardSet("Bias", 0.5)
  tie(op, "BlowMeta", blowMeta, "Out")
  self:addMonoBranch("blowMeta", blowMeta, "In", blowMeta, "Out")

  -- Strike
  local strikeLevel = self:addObject("strikeLevel", app.ParameterAdapter())
  strikeLevel:hardSet("Bias", 0.5)
  tie(op, "StrikeLevel", strikeLevel, "Out")
  self:addMonoBranch("strikeLevel", strikeLevel, "In", strikeLevel, "Out")

  local strikeTimbre = self:addObject("strikeTimbre", app.ParameterAdapter())
  strikeTimbre:hardSet("Bias", 0.5)
  tie(op, "StrikeTimbre", strikeTimbre, "Out")
  self:addMonoBranch("strikeTimbre", strikeTimbre, "In", strikeTimbre, "Out")

  local strikeMeta = self:addObject("strikeMeta", app.ParameterAdapter())
  strikeMeta:hardSet("Bias", 0.5)
  tie(op, "StrikeMeta", strikeMeta, "Out")
  self:addMonoBranch("strikeMeta", strikeMeta, "In", strikeMeta, "Out")

  -- Envelope
  local envShape = self:addObject("envShape", app.ParameterAdapter())
  envShape:hardSet("Bias", 0.5)
  tie(op, "EnvShape", envShape, "Out")
  self:addMonoBranch("envShape", envShape, "In", envShape, "Out")

  -- Resonator params (affect exciter behavior)
  local damping = self:addObject("damping", app.ParameterAdapter())
  damping:hardSet("Bias", 0.5)
  tie(op, "Damping", damping, "Out")
  self:addMonoBranch("damping", damping, "In", damping, "Out")

  local brightness = self:addObject("brightness", app.ParameterAdapter())
  brightness:hardSet("Bias", 0.5)
  tie(op, "Brightness", brightness, "Out")
  self:addMonoBranch("brightness", brightness, "In", brightness, "Out")
end

function Commotio:onLoadViews()
  return {
    gate = Gate {
      button      = "gate",
      description = "Gate",
      branch      = self.branches.gate,
      comparator  = self.objects.gate
    },
    bowLevel = GainBias {
      button        = "bow",
      description   = "Bow Level",
      branch        = self.branches.bowLevel,
      gainbias      = self.objects.bowLevel,
      range         = self.objects.bowLevel,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.0
    },
    bowTimbre = GainBias {
      button        = "b.tmb",
      description   = "Bow Timbre",
      branch        = self.branches.bowTimbre,
      gainbias      = self.objects.bowTimbre,
      range         = self.objects.bowTimbre,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    blowLevel = GainBias {
      button        = "blow",
      description   = "Blow Level",
      branch        = self.branches.blowLevel,
      gainbias      = self.objects.blowLevel,
      range         = self.objects.blowLevel,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.0
    },
    blowTimbre = GainBias {
      button        = "bl.tm",
      description   = "Blow Timbre",
      branch        = self.branches.blowTimbre,
      gainbias      = self.objects.blowTimbre,
      range         = self.objects.blowTimbre,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    blowMeta = GainBias {
      button        = "bl.mt",
      description   = "Blow Meta",
      branch        = self.branches.blowMeta,
      gainbias      = self.objects.blowMeta,
      range         = self.objects.blowMeta,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    strikeLevel = GainBias {
      button        = "strk",
      description   = "Strike Level",
      branch        = self.branches.strikeLevel,
      gainbias      = self.objects.strikeLevel,
      range         = self.objects.strikeLevel,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    strikeTimbre = GainBias {
      button        = "s.tmb",
      description   = "Strike Timbre",
      branch        = self.branches.strikeTimbre,
      gainbias      = self.objects.strikeTimbre,
      range         = self.objects.strikeTimbre,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    strikeMeta = GainBias {
      button        = "s.mt",
      description   = "Strike Meta",
      branch        = self.branches.strikeMeta,
      gainbias      = self.objects.strikeMeta,
      range         = self.objects.strikeMeta,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    envShape = GainBias {
      button        = "env",
      description   = "Envelope",
      branch        = self.branches.envShape,
      gainbias      = self.objects.envShape,
      range         = self.objects.envShape,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    damping = GainBias {
      button        = "damp",
      description   = "Damping",
      branch        = self.branches.damping,
      gainbias      = self.objects.damping,
      range         = self.objects.damping,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    brightness = GainBias {
      button        = "brit",
      description   = "Brightness",
      branch        = self.branches.brightness,
      gainbias      = self.objects.brightness,
      range         = self.objects.brightness,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    }
  }, {
    expanded  = { "gate", "bowLevel", "bowTimbre", "blowLevel", "blowTimbre",
                  "blowMeta", "strikeLevel", "strikeTimbre", "strikeMeta",
                  "envShape", "damping", "brightness" },
    collapsed = {}
  }
end

return Commotio
