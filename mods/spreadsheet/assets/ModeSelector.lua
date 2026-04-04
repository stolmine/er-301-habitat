local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"

local ModeSelector = Class {}
ModeSelector:include(GainBias)

function ModeSelector:init(args)
  self.modeNames = args.modeNames
  GainBias.init(self, args)
  self:updateLabel()
end

function ModeSelector:updateLabel()
  local value = math.floor(self.bias:getValueInUnits() + 0.5)
  local name = self.modeNames[value]
  if name then
    self.fader:setLabel(name)
  end
end

function ModeSelector:encoder(change, shifted)
  GainBias.encoder(self, change, shifted)
  self:updateLabel()
  return true
end

function ModeSelector:spotReleased(spot, shifted)
  local result = GainBias.spotReleased(self, spot, shifted)
  self:updateLabel()
  return result
end

function ModeSelector:subReleased(i, shifted)
  local result = GainBias.subReleased(self, i, shifted)
  self:updateLabel()
  return result
end

return ModeSelector
