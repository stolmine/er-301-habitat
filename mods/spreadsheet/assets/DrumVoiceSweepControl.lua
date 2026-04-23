local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col2 = app.BUTTON2_CENTER

local DrumVoiceSweepControl = Class {}
DrumVoiceSweepControl:include(GainBias)

function DrumVoiceSweepControl:init(args)
  GainBias.init(self, args)

  self.paramMode, self.shiftHeld, self.shiftUsed = false, false, false
  self.normalSubGraphic = self.subGraphic
  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Sweep Time", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.paramSubGraphic:addChild(desc)

  local timeMap = app.LinearDialMap(0.001, 0.5)
  timeMap:setSteps(0.05, 0.01, 0.001, 0.001)

  self.timeReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.sweepTimeParam)
    g:setAttributes(app.unitSecs, timeMap)
    g:setPrecision(3)
    g:setCenter(col2, center4)
    return g
  end)()
  self.paramModeDefaultSub = self.timeReadout

  self.paramSubGraphic:addChild(self.timeReadout)
  self.paramSubGraphic:addChild(app.SubButton("time", 2))
end

function DrumVoiceSweepControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode, self.paramFocusedReadout = enabled, nil
  self:setSubCursorController(nil)
  self.subGraphic = enabled and self.paramSubGraphic or self.normalSubGraphic
  if not enabled then self:setFocusedReadout(self.bias) end
  self:addSubGraphic(self.subGraphic)
end

function DrumVoiceSweepControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then self:setSubCursorController(self.paramModeDefaultSub) end
end

function DrumVoiceSweepControl:onCursorLeave(spot)
  if self.paramMode then self.paramFocusedReadout = nil; self:setSubCursorController(nil) end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function DrumVoiceSweepControl:shiftPressed()
  self.shiftHeld, self.shiftUsed = true, false
  self.shiftSnapshot = self.paramFocusedReadout and self.paramFocusedReadout:getValueInUnits() or nil
  return true
end

function DrumVoiceSweepControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.paramFocusedReadout and self.shiftSnapshot then
      if self.paramFocusedReadout:getValueInUnits() ~= self.shiftSnapshot then
        self.shiftHeld, self.shiftSnapshot = false, nil; return true
      end
    end
    self:setParamMode(not self.paramMode)
  end
  self.shiftHeld, self.shiftSnapshot = false, nil; return true
end

function DrumVoiceSweepControl:subReleased(i, shifted)
  if self.paramMode then
    if i == 2 and self.timeReadout then
      if shifted then ShiftHelpers.openKeyboardFor(self.timeReadout, "sweep time")
      else self.timeReadout:save(); self.paramFocusedReadout = self.timeReadout
        self:setSubCursorController(self.timeReadout)
        if not self:hasFocus("encoder") then self:focus() end
      end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function DrumVoiceSweepControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil; self:setSubCursorController(nil); self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function DrumVoiceSweepControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine); return true
  end
  return GainBias.encoder(self, change, shifted)
end

function DrumVoiceSweepControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then self.paramFocusedReadout:zero(); return true end
  return GainBias.zeroPressed(self)
end

function DrumVoiceSweepControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then self.paramFocusedReadout:restore(); return true end
  return GainBias.cancelReleased(self, shifted)
end

return DrumVoiceSweepControl
