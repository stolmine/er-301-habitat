-- ShiftHelpers -- open a numeric keyboard for a param-mode readout on shift+sub.
-- Copied from the spreadsheet package (shift convention). Mirrors GainBias's
-- doGainSet / doBiasSet keyboard-set semantics.

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
