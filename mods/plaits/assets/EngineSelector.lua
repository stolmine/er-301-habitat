local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"

local EngineSelector = Class {}
EngineSelector:include(GainBias)

function EngineSelector:init(args)
  self.engineNames = args.engineNames
  GainBias.init(self, args)
  self:updateLabel()
end

function EngineSelector:updateLabel()
  local value = math.floor(self.bias:getValueInUnits() + 0.5)
  local name = self.engineNames[value]
  if name then
    self.fader:setLabel(name)
  end
end

function EngineSelector:encoder(change, shifted)
  GainBias.encoder(self, change, shifted)
  self:updateLabel()
  return true
end

function EngineSelector:spotReleased(spot, shifted)
  local result = GainBias.spotReleased(self, spot, shifted)
  self:updateLabel()
  return result
end

function EngineSelector:subReleased(i, shifted)
  local result = GainBias.subReleased(self, i, shifted)
  self:updateLabel()
  return result
end

return EngineSelector
