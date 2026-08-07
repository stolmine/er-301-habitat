local Class = require "Base.Class"
local Encoder = require "Encoder"
local GainBias = require "Unit.ViewControl.GainBias"

local ModeSelector = Class {}
ModeSelector:include(GainBias)

function ModeSelector:init(args)
  self.modeNames = args.modeNames
  -- Optional discrete encoder mode. Scaling the dial-map step (the
  -- obvious approach) fights the encoder's acceleration, so fast turns
  -- still jump several options. The firmware's discrete navigation
  -- instead accumulates the RAW encoder change and steps once per
  -- `discreteThreshold` ticks — see Env.EncoderThreshold (default 3),
  -- xroot/Unit/Editor.lua ItemHeader, xroot/ListWindow.lua. That is
  -- acceleration-independent, so it feels deliberate. Pass a larger
  -- threshold for a less twitchy selector. Use with an integer biasMap.
  self.discrete = args.discrete
  self.discreteThreshold = args.discreteThreshold or 3
  -- `normalized`: the underlying parameter carries 0-1 instead of the raw
  -- mode index, so a 0-1 CV sweeps the whole list (a 0-49 parameter would
  -- barely move under one volt). The selector still presents and steps
  -- INDICES; only the stored units change. Requires modeNames to span the
  -- full index range.
  self.normalized = args.normalized
  -- `discreteCoarseStep`: how many modes one coarse step covers. Long lists
  -- with internal structure (Vitrail's routing is 2 halves x 5x5 filter pairs)
  -- want coarse to travel a whole SET at a time and fine to pick within it,
  -- so the two resolutions land on meaningful boundaries instead of the coarse
  -- turn skipping past entries at an unrelated stride. Default 1 = the old
  -- single-mode behaviour for every existing consumer.
  self.discreteCoarseStep = args.discreteCoarseStep or 1
  -- `discreteJumpStep`: entries moved per step while SHIFT is held. A discrete
  -- list cannot be subdivided, so the encoder's four resolutions cannot be four
  -- step VALUES the way a continuous dial's are. They become two axes instead:
  -- how many entries a step moves, and how much encoder travel one step costs.
  self.discreteJumpStep = args.discreteJumpStep
  GainBias.init(self, args)
  self.encoderSum = 0
  -- Range derived from modeNames keys (so we don't depend on the dial map
  -- exposing min/max through SWIG). Needed by both discrete and normalized.
  local lo, hi
  for k in pairs(self.modeNames or {}) do
    if type(k) == "number" then
      if not lo or k < lo then lo = k end
      if not hi or k > hi then hi = k end
    end
  end
  self.discreteMin = lo or 0
  self.discreteMax = hi or 1
  self.indexSpan = self.discreteMax - self.discreteMin
  self:updateLabel()
end

-- Current mode INDEX, whatever the parameter's units are.
function ModeSelector:currentIndex()
  local v = self.bias:getValueInUnits()
  if self.normalized and self.indexSpan > 0 then
    return self.discreteMin + math.floor(v * self.indexSpan + 0.5)
  end
  return math.floor(v + 0.5)
end

-- Write a mode INDEX back through whatever units the parameter carries.
function ModeSelector:setIndex(idx)
  if self.normalized and self.indexSpan > 0 then
    self.bias:setValueInUnits((idx - self.discreteMin) / self.indexSpan)
  else
    self.bias:setValueInUnits(idx)
  end
end

function ModeSelector:updateLabel()
  local name = self.modeNames[self:currentIndex()]
  if name then
    self.fader:setLabel(name)
  end
end

function ModeSelector:stepDiscrete(dir)
  local cur = self:currentIndex()
  local v = cur + dir
  if v < self.discreteMin then v = self.discreteMin end
  if v > self.discreteMax then v = self.discreteMax end
  if v ~= cur then
    self.bias:save()
    self:setIndex(v)
    self:updateLabel()
  end
end

function ModeSelector:encoder(change, shifted)
  -- Only the BIAS carries the mode index. If the GAIN readout is focused the
  -- user is editing CV depth, so fall through to GainBias and let it edit what
  -- they are actually looking at. Intercepting unconditionally meant turning
  -- the gain knob moved the mode instead.
  if self.discrete and self.focusedReadout ~= self.gain then
    -- Three resolutions, expressed as (step size, travel per step) because a
    -- discrete list has no fractional positions to reach for:
    --   shift  -> jump a whole group (discreteJumpStep), for crossing a long list
    --   coarse -> one step of discreteCoarseStep at the normal threshold (default)
    --   fine   -> the same step at DOUBLE the threshold, so landing on a
    --             particular entry takes deliberate travel and cannot overshoot
    local step, threshold = self.discreteCoarseStep, self.discreteThreshold
    if shifted and self.discreteJumpStep then
      step = self.discreteJumpStep
    elseif self.encoderState == Encoder.Fine then
      threshold = self.discreteThreshold * 2
    end
    self.encoderSum = self.encoderSum + change
    if self.encoderSum > threshold then
      self.encoderSum = 0
      self:stepDiscrete(step)
    elseif self.encoderSum < -threshold then
      self.encoderSum = 0
      self:stepDiscrete(-step)
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
