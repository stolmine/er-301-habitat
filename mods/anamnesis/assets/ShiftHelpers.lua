-- ShiftHelpers -- shared shift-convention utility for Anamnesis Pattern-A
-- controls (planning/shift-handling.md, Decision 5 B). Copied locally to keep
-- the package self-contained (no cross-package require, per the Pecto lesson).
-- Opens a numeric keyboard to set a param-mode readout's value, mirroring the
-- stock GainBias shift+sub keyboard-set semantics.

local Decimal = require "Keyboard.Decimal"

local M = {}

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
