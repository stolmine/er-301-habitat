-- DiscreteStep — the habitat discrete-stepping standard, for readouts that live
-- on a paramMode SUB-DISPLAY.
--
-- Why this exists: a paramMode sub-display routes the encoder straight to the
-- focused app.Readout, which steps by dial-map value and therefore inherits the
-- encoder's ACCELERATION — a fast turn jumps several entries. That is fine for a
-- continuous value and wrong for a readout addressing a list. Scaling the map's
-- step does not fix it; it fights the acceleration rather than ignoring it.
--
-- The standard (planning/discrete-control-standard-inventory.md), taken from
-- Vitrail's routing selector:
--   coarse = 1 entry per `threshold` RAW encoder ticks (8, ~2 detents)
--   fine   = 1 entry per 2*threshold, so landing on one entry is deliberate
-- Both are exact index multiples, so neither resolution can land between
-- entries or skip one. A discrete list has no fractional positions, so "fine"
-- can only mean more travel per step, never a smaller step.
--
-- NOTE the same value is often reachable TWICE — once here on a sub-display and
-- once as an expanded ModeSelector fader. Both surfaces need the treatment;
-- fixing only the ModeSelector leaves the one you reach first untouched.

local Encoder = require "Encoder"

local DiscreteStep = {}

DiscreteStep.threshold = 8

-- Accumulate `change` on `control` and step `readout` by whole entries.
-- `lo`/`hi` are inclusive integer bounds. Always returns true (handled).
function DiscreteStep.encoder(control, readout, change, lo, hi)
  local threshold = DiscreteStep.threshold
  if control.encoderState == Encoder.Fine then
    threshold = threshold * 2
  end

  control.discreteSum = (control.discreteSum or 0) + change

  local dir = 0
  if control.discreteSum > threshold then
    dir = 1
  elseif control.discreteSum < -threshold then
    dir = -1
  end
  if dir == 0 then
    return true
  end
  control.discreteSum = 0

  local cur = math.floor(readout:getValueInUnits() + 0.5)
  local v = cur + dir
  if v < lo then v = lo end
  if v > hi then v = hi end
  if v ~= cur then
    readout:save()
    readout:setValueInUnits(v)
  end
  return true
end

-- Drop a part-turn when focus moves, so leftover ticks on one readout cannot
-- carry into the next one the user selects.
function DiscreteStep.reset(control)
  control.discreteSum = 0
end

return DiscreteStep
