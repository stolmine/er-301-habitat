local Class = require "Base.Class"
local ModeSelector = require "stolmine.ModeSelector"

local MacroControl = Class {}
MacroControl:include(ModeSelector)

function MacroControl:init(args)
  self.applyPreset = args.applyPreset
  self.lastValue = -1
  ModeSelector.init(self, args)
  -- Apply initial preset
  local value = math.floor(self.bias:getValueInUnits() + 0.5)
  self.lastValue = value
end

function MacroControl:encoder(change, shifted)
  ModeSelector.encoder(self, change, shifted)
  local value = math.floor(self.bias:getValueInUnits() + 0.5)
  if value ~= self.lastValue then
    self.lastValue = value
    if self.applyPreset then self.applyPreset(value) end
  end
  return true
end

function MacroControl:spotReleased(spot, shifted)
  local result = ModeSelector.spotReleased(self, spot, shifted)
  local value = math.floor(self.bias:getValueInUnits() + 0.5)
  if value ~= self.lastValue then
    self.lastValue = value
    if self.applyPreset then self.applyPreset(value) end
  end
  return result
end

function MacroControl:subReleased(i, shifted)
  local result = ModeSelector.subReleased(self, i, shifted)
  local value = math.floor(self.bias:getValueInUnits() + 0.5)
  if value ~= self.lastValue then
    self.lastValue = value
    if self.applyPreset then self.applyPreset(value) end
  end
  return result
end

return MacroControl
