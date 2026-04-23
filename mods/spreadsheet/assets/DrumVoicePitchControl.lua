local app = app
local Class = require "Base.Class"
local Pitch = require "Unit.ViewControl.Pitch"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col2 = app.BUTTON2_CENTER

local DrumVoicePitchControl = Class {}
DrumVoicePitchControl:include(Pitch)

function DrumVoicePitchControl:init(args)
  Pitch.init(self, args)

  self.paramMode = false
  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local octaveMap = (function()
    local m = app.LinearDialMap(-4, 4)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  self.octaveReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.octaveParam)
    g:setAttributes(app.unitNone, octaveMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    return g
  end)()

  local desc = app.Label("Octave Offset", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  self.paramSubGraphic:addChild(self.octaveReadout)
  self.paramSubGraphic:addChild(desc)
  self.paramSubGraphic:addChild(app.SubButton("oct", 2))

  self.paramModeDefaultSub = self.octaveReadout
end

function DrumVoicePitchControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.paramSubGraphic
  else
    self.subGraphic = self.normalSubGraphic
  end
  self:addSubGraphic(self.subGraphic)
end

function DrumVoicePitchControl:onCursorEnter(spot)
  Pitch.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    self:setSubCursorController(self.paramModeDefaultSub)
  end
end

function DrumVoicePitchControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return Pitch.spotReleased(self, spot, shifted)
end

function DrumVoicePitchControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  Pitch.onCursorLeave(self, spot)
end

function DrumVoicePitchControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function DrumVoicePitchControl:shiftReleased()
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

function DrumVoicePitchControl:subReleased(i, shifted)
  if self.paramMode then
    if i == 2 then
      if shifted then
        ShiftHelpers.openKeyboardFor(self.octaveReadout, "octave")
      else
        self.octaveReadout:save()
        self.paramFocusedReadout = self.octaveReadout
        self:setSubCursorController(self.octaveReadout)
        if not self:hasFocus("encoder") then self:focus() end
      end
    end
    return true
  end
  return Pitch.subReleased(self, i, shifted)
end

function DrumVoicePitchControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return Pitch.encoder(self, change, shifted)
end

function DrumVoicePitchControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return Pitch.zeroPressed(self)
end

function DrumVoicePitchControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return Pitch.cancelReleased(self, shifted)
end

return DrumVoicePitchControl
