-- Fabula Mix control: a GainBias whose main dial is Dry/Wet, with a single
-- secondary parameter (HPF, the wet highpass corner in Hz) revealed on tap-shift.
-- Lean single-sub version of spreadsheet/MixControl.lua (same proven paramMode /
-- shift / focus logic). HPF is deliberately NOT an xform target.

local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER

local MixHpfControl = Class {}
MixHpfControl:include(GainBias)

function MixHpfControl:init(args)
  GainBias.init(self, args)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false

  -- Keep the GainBias subGraphic as the normal (Dry/Wet) mode graphic.
  self.normalSubGraphic = self.subGraphic

  -- Param-mode subGraphic: the HPF readout.
  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local hpfMap = (function()
    local m = app.LinearDialMap(20, 500)
    m:setSteps(50, 10, 1, 1)
    return m
  end)()

  self.hpfReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.hpfParam)
    g:setAttributes(app.unitHertz, hpfMap)
    g:setPrecision(0)
    g:setCenter(col1, center4)
    return g
  end)()

  local desc = app.Label("Wet Highpass", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  self.paramSubGraphic:addChild(desc)
  self.paramSubGraphic:addChild(self.hpfReadout)
  self.paramSubGraphic:addChild(app.SubButton("HPF", 1))
end

function MixHpfControl:setParamMode(enabled)
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

function MixHpfControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    -- Strict convention on re-entry: encoder edits Dry/Wet until HPF is tapped
    -- (avoids the phantom-caret / navigation issue in fabula-overview-caret).
    self:setSubCursorController(nil)
  end
end

function MixHpfControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function MixHpfControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function MixHpfControl:shiftReleased()
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

function MixHpfControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function MixHpfControl:subReleased(i, shifted)
  if self.paramMode then
    if i == 1 then
      if shifted then
        ShiftHelpers.openKeyboardFor(self.hpfReadout, "HPF")
      else
        self.hpfReadout:save()
        self.paramFocusedReadout = self.hpfReadout
        self:setSubCursorController(self.hpfReadout)
        if not self:hasFocus("encoder") then self:focus() end
      end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function MixHpfControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function MixHpfControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function MixHpfControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return MixHpfControl
