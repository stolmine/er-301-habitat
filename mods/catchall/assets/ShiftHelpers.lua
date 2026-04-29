-- ShiftHelpers -- shared utilities for the spreadsheet shift convention.
--
-- See planning/shift-handling.md. Used by Pattern A controls to open a
-- numeric keyboard for a param-mode readout on shift+sub (Decision 5 B,
-- uniform with stock GainBias shift+sub2/sub3 keyboard-set semantics).

local Decimal = require "Keyboard.Decimal"

local M = {}

-- Open a numeric keyboard to set the given readout's value. Mirrors the
-- GainBias:doGainSet / doBiasSet pattern.
--
--   readout: a Readout widget exposing :save, :getValueInUnits,
--            :setValueInUnits.
--   label:   human-readable parameter name for the keyboard prompt.
function M.openKeyboardFor(readout, label)
  local name = label or "value"
  local kb = Decimal {
    message = string.format("Set '%s'.", name),
    commitMessage = string.format("%s updated.", name),
    initialValue = readout:getValueInUnits()
  }
  local task = function(value)
    if value then
      readout:save()
      readout:setValueInUnits(value)
    end
  end
  kb:subscribe("done", task)
  kb:subscribe("commit", task)
  kb:show()
end

return M
