-- AnamSubControl -- canonical Pattern A (planning/shift-handling.md): a GainBias
-- MAIN with a custom param sub-display reached by tapping SHIFT. Tapping a
-- sub-button focuses that sub-readout (encoder edits it); shift+sub opens the
-- numeric keyboard; tapping shift with no encoder activity toggles back to the
-- stock sub-display.
--
-- Generic over the sub list so one class serves every multi-sub ply:
--   subs = { { param = <Parameter>, button = "dcy", map = <DialMap>,
--              precision = 2, units = app.unitNone }, ... }   -- up to 3
--
-- Modeled verbatim on spreadsheet/DriveControl.lua; mechanics + gotchas per
-- the locked shift-handling spec (NEVER assign self.focusedReadout directly;
-- paramMode is session-only; Decision 1B suppresses the toggle on any encoder
-- touch during shift-hold; Decision 8 clears the spurious sub3 highlight).

local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "anamnesis.ShiftHelpers"

local ply = app.SECTION_PLY
local center4 = app.GRID5_CENTER4
local cols = { app.BUTTON1_CENTER, app.BUTTON2_CENTER, app.BUTTON3_CENTER }

local AnamSubControl = Class {}
AnamSubControl:include(GainBias)

function AnamSubControl:init(args)
  GainBias.init(self, args)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic
  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  self.subReadouts = {}
  self.subNames = {}
  for i, sub in ipairs(args.subs or {}) do
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(sub.param)
    g:setAttributes(sub.units or app.unitNone, sub.map)
    g:setPrecision(sub.precision or 2)
    g:setCenter(cols[i], center4)
    self.paramSubGraphic:addChild(g)
    self.paramSubGraphic:addChild(app.SubButton(sub.button, i))
    self.subReadouts[i] = g
    self.subNames[i] = sub.button
  end
end

function AnamSubControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.paramSubGraphic
  else
    self.subGraphic = self.normalSubGraphic
    self:setFocusedReadout(self.bias)
  end
  self:addSubGraphic(self.subGraphic)
end

function AnamSubControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then self:setSubCursorController(self.paramModeDefaultSub) end
end

function AnamSubControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function AnamSubControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function AnamSubControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.paramFocusedReadout and self.shiftSnapshot then
      local cur = self.paramFocusedReadout:getValueInUnits()
      if cur ~= self.shiftSnapshot then
        self.shiftHeld = false
        self.shiftSnapshot = nil
        return true
      end
    end
    self:setParamMode(not self.paramMode)
  end
  self.shiftHeld = false
  self.shiftSnapshot = nil
  return true
end

function AnamSubControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function AnamSubControl:subReleased(i, shifted)
  if self.paramMode then
    local readout = self.subReadouts[i]
    local name = self.subNames[i]
    if readout then
      if shifted then
        ShiftHelpers.openKeyboardFor(readout, name)
      else
        readout:save()
        self.paramFocusedReadout = readout
        self:setSubCursorController(readout)
        if not self:hasFocus("encoder") then self:focus() end
      end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function AnamSubControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function AnamSubControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function AnamSubControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return AnamSubControl
