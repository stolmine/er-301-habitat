local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"

local ModeSelector = Class {}
ModeSelector:include(GainBias)

function ModeSelector:init(args)
  self.modeNames = args.modeNames
  -- Optional discrete encoder mode. Scaling the dial-map step (the
  -- obvious approach) fights the encoder's acceleration, so fast turns
  -- still jump several options. The firmware's discrete navigation
  -- instead accumulates the RAW encoder change and steps once per
  -- `discreteThreshold` ticks — acceleration-independent, so it feels
  -- deliberate. On hardware the eQEP runs 4x quadrature (~4 counts per
  -- physical detent), so threshold ~= 4 x (detents per step); 16 (the
  -- habitat standard, see spreadsheet MultibandSaturator/Parfait) ~= 4
  -- detents per step. Pass a smaller threshold for a faster, less
  -- tedious sweep through a long list. Use with an integer biasMap.
  self.discrete = args.discrete
  self.discreteThreshold = args.discreteThreshold or 3
  GainBias.init(self, args)
  self.encoderSum = 0
  if self.discrete then
    -- Range derived from modeNames keys (so we don't depend on the
    -- dial map exposing min/max through SWIG).
    local lo, hi
    for k in pairs(self.modeNames or {}) do
      if type(k) == "number" then
        if not lo or k < lo then lo = k end
        if not hi or k > hi then hi = k end
      end
    end
    self.discreteMin = lo or 0
    self.discreteMax = hi or 1
  end
  self:updateLabel()
end

function ModeSelector:updateLabel()
  local value = math.floor(self.bias:getValueInUnits() + 0.5)
  local name = self.modeNames[value]
  if name then
    self.fader:setLabel(name)
  end
end

function ModeSelector:stepDiscrete(dir)
  local cur = math.floor(self.bias:getValueInUnits() + 0.5)
  local v = cur + dir
  if v < self.discreteMin then v = self.discreteMin end
  if v > self.discreteMax then v = self.discreteMax end
  if v ~= cur then
    self.bias:save()
    self.bias:setValueInUnits(v)
    self:updateLabel()
  end
end

function ModeSelector:encoder(change, shifted)
  if self.discrete then
    self.encoderSum = self.encoderSum + change
    if self.encoderSum > self.discreteThreshold then
      self.encoderSum = 0
      self:stepDiscrete(1)
    elseif self.encoderSum < -self.discreteThreshold then
      self.encoderSum = 0
      self:stepDiscrete(-1)
    end
    return true
  end
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
