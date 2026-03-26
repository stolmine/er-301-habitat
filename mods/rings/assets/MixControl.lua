local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"

local MixControl = Class {}
MixControl:include(GainBias)

function MixControl:init(args)
  self.monoLabels = args.monoLabels
  self.stereoLabels = args.stereoLabels
  self.stereoOption = args.stereoOption
  GainBias.init(self, args)
  self:updateLabel()
end

function MixControl:updateLabel()
  local value = self.bias:getValueInUnits()
  local labels = self.stereoOption and self.stereoOption:value() == 1
      and self.stereoLabels or self.monoLabels

  local label
  if value < -0.66 then
    label = labels[1]
  elseif value < -0.33 then
    label = labels[2]
  elseif value < 0.33 then
    label = labels[3]
  elseif value < 0.66 then
    label = labels[4]
  else
    label = labels[5]
  end

  if label then
    self.fader:setLabel(label)
  end
end

function MixControl:encoder(change, shifted)
  GainBias.encoder(self, change, shifted)
  self:updateLabel()
  return true
end

function MixControl:spotReleased(spot, shifted)
  local result = GainBias.spotReleased(self, spot, shifted)
  self:updateLabel()
  return result
end

function MixControl:subReleased(i, shifted)
  local result = GainBias.subReleased(self, i, shifted)
  self:updateLabel()
  return result
end

return MixControl
