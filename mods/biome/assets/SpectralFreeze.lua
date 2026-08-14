local app = app
local libbiome = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Encoder = require "Encoder"
local MenuHeader = require "Unit.MenuControl.Header"
local OptionControl = require "Unit.MenuControl.OptionControl"

local shiftMap = (function()
  local map = app.LinearDialMap(-48, 24)
  map:setSteps(12, 1, 0.1, 0.01)
  return map
end)()

local SpectralFreeze = Class {}
SpectralFreeze:include(Unit)

function SpectralFreeze:init(args)
  args.title = "Spectral Freeze"
  args.mnemonic = "SF"
  Unit.init(self, args)
end

function SpectralFreeze:onLoadGraph(channelCount)
  local op = self:addObject("op", libbiome.SpectralFreeze())

  connect(self, "In1", op, "In")
  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end

  -- Freeze gate. Toggle mode so a press latches and a second press releases,
  -- matching the reference's manual behaviour.
  local freeze = self:addObject("freeze", app.Comparator())
  freeze:setToggleMode()
  connect(freeze, "Out", op, "Freeze")
  self:addMonoBranch("freeze", freeze, "In", freeze, "Out")

  local function adapter(name, param, bias)
    local o = self:addObject(name, app.ParameterAdapter())
    o:hardSet("Gain", 0.0)   -- CV is opt-in catalog-wide
    o:hardSet("Bias", bias)
    tie(op, param, o, "Out")
    self:addMonoBranch(name, o, "In", o, "Out")
    return o
  end

  adapter("depth", "Depth", 1.0)
  adapter("rate", "Rate", 0.25)
  adapter("offset", "Offset", 0.0)
  adapter("ether", "Ether", 1.0)
  adapter("attack", "Attack", 0.05)
  adapter("release", "Release", 0.2)
  adapter("shift", "Shift", 0.0)
  adapter("mix", "Mix", 0.5)
end

function SpectralFreeze:onLoadMenu(objects, branches)
  return {
    movementHeader = MenuHeader { description = "Movement" },
    movement = OptionControl {
      description = "Movement",
      option = objects.op:getOption("Movement"),
      choices = { "Forwards", "Backwards", "Alternating", "Random walk", "Random skip" },
      boolean = false
    }
  }, { "movementHeader", "movement" }
end

function SpectralFreeze:onLoadViews()
  local function gb(key, description, bias, map, prec)
    return GainBias {
      button        = key,
      description   = description,
      branch        = self.branches[key],
      gainbias      = self.objects[key],
      range         = self.objects[key],
      biasMap       = map or Encoder.getMap("[0,1]"),
      biasPrecision = prec or 2,
      initialBias   = bias
    }
  end
  return {
    freeze = Gate {
      button      = "freeze",
      description = "Freeze",
      branch      = self.branches.freeze,
      comparator  = self.objects.freeze
    },
    -- How far back through the captured history the motion may reach.
    depth   = gb("depth",   "Depth",   1.0),
    rate    = gb("rate",    "Rate",    0.25),
    offset  = gb("offset",  "Offset",  0.0),
    -- Transient rejection. 1 keeps everything; lowering it drives the result
    -- toward a handful of the strongest steady partials.
    ether   = gb("ether",   "Ether",   1.0),
    attack  = gb("attack",  "Attack",  0.05),
    release = gb("release", "Release", 0.2),
    shift   = gb("shift",   "Shift",   0.0, shiftMap, 1),
    mix     = gb("mix",     "Mix",     0.5)
  }, {
    expanded  = { "freeze", "depth", "rate", "offset", "ether", "shift", "mix" },
    collapsed = {}
  }
end

return SpectralFreeze
