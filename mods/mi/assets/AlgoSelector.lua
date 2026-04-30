local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"

local AlgoSelector = Class {}
AlgoSelector:include(GainBias)

function AlgoSelector:init(args)
  self.algoNameFn = args.algoNameFn
  GainBias.init(self, args)
  self:updateLabel()
end

function AlgoSelector:updateLabel()
  local value = self.bias:getValueInUnits()
  local name = self.algoNameFn(value)
  if name then
    self.fader:setLabel(name)
  end
end

function AlgoSelector:encoder(change, shifted)
  GainBias.encoder(self, change, shifted)
  self:updateLabel()
  return true
end

function AlgoSelector:spotReleased(spot, shifted)
  local result = GainBias.spotReleased(self, spot, shifted)
  self:updateLabel()
  return result
end

function AlgoSelector:subReleased(i, shifted)
  local result = GainBias.subReleased(self, i, shifted)
  self:updateLabel()
  return result
end

return AlgoSelector
