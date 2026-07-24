-- Shared builder for the Fade Mixer family (4 / 6 / 8 inputs). One C++ FadeMixer
-- object (8 inlets + an Inputs count); each unit fixes the count and wires that
-- many channels. Mute/solo is UNIT-LOCAL (a MuteGroup the unit owns, driven by
-- FadeMuteMeter) so it acts only across this mixer's own inputs.
local app = app
local libbiome = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local MuteGroup = require "Chain.MuteGroup"
local FadeMuteMeter = require "biome.FadeMuteMeter"

local function fadeMap()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.0001)
  return m
end

local function levelMap()
  local m = app.LinearDialMap(0, 4)
  m:setSteps(1, 0.1, 0.01, 0.001)
  return m
end

-- Build and return a Fade Mixer unit Class for `nInputs` (4/6/8).
return function(nInputs, title, mnemonic)
  local FadeMixerN = Class {}
  FadeMixerN:include(Unit)

  function FadeMixerN:init(args)
    args.title = title
    args.mnemonic = mnemonic
    Unit.init(self, args)
  end

  function FadeMixerN:onLoadGraph(channelCount)
    local op = self:addObject("op", libbiome.FadeMixer())
    op:hardSet("Inputs", nInputs)

    local fade = self:addObject("fade", app.ParameterAdapter())
    local level = self:addObject("level", app.ParameterAdapter())
    fade:hardSet("Bias", 0.0)
    level:hardSet("Bias", 1.0)

    -- N input channels: a fader gain per channel, its own mono branch, connected
    -- to the matching op inlet.
    for c = 1, nInputs do
      local g = self:addObject("gain" .. c, app.ConstantGain())
      g:setClampInDecibels(-59.9)
      g:hardSet("Gain", 1.0)
      connect(g, "Out", op, "In" .. c)
      self:addMonoBranch("ch" .. c, g, "In", g, "Out")
    end

    -- Chain passthrough + crossfaded mix (preserves the original unit's summing
    -- behaviour: the incoming chain signal plus the crossfade output).
    local sum = self:addObject("sum", app.Sum())
    connect(self, "In1", sum, "Left")
    connect(op, "Out", sum, "Right")
    connect(sum, "Out", self, "Out1")
    if channelCount > 1 then
      connect(sum, "Out", self, "Out2")
    end

    tie(op, "Fade", fade, "Out")
    tie(op, "Level", level, "Out")
    self:addMonoBranch("fade", fade, "In", fade, "Out")
    self:addMonoBranch("level", level, "In", level, "Out")
  end

  function FadeMixerN:onLoadViews(objects, branches)
    local controls = {}
    -- Unit-local mute group: mute/solo act only across THIS mixer's inputs.
    self.localMuteGroup = MuteGroup()

    local expanded = {}
    for c = 1, nInputs do
      local id = "ch" .. c
      controls[id] = FadeMuteMeter {
        button = "in" .. c,
        branch = branches[id],
        faderParam = objects["gain" .. c]:getParameter("Gain"),
        muteGroup = self.localMuteGroup
      }
      self.localMuteGroup:add(controls[id])
      expanded[c] = id
    end

    controls.fade = GainBias {
      button = "fade",
      branch = branches.fade,
      description = "Fade",
      gainbias = objects.fade,
      range = objects.fade,
      biasMap = fadeMap(),
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
      biasMap = levelMap(),
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 1.0
    }
    expanded[#expanded + 1] = "fade"
    expanded[#expanded + 1] = "level"

    return controls, { expanded = expanded, collapsed = {} }
  end

  return FadeMixerN
end
