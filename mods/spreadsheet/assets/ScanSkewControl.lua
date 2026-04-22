-- ScanSkewControl -- Blanda's Scan ply with the global Skew macro
-- accessible on shift. GainBias-based: main-view encoder edits the
-- global Scan bias by default. Shift-toggle reveals a single centered
-- Skew readout with a sub-button to focus it.

local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col2 = app.BUTTON2_CENTER

local ScanSkewControl = Class {}
ScanSkewControl:include(GainBias)

function ScanSkewControl:init(args)
  GainBias.init(self, args)

  self:setClassName("ScanSkewControl")

  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Skew", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.paramSubGraphic:addChild(desc)

  local skewMap = args.skewMap or (function()
    local m = app.LinearDialMap(-1, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.0001)
    return m
  end)()

  self.skewReadout = app.Readout(0, 0, ply, 10)
  self.skewReadout:setParameter(args.skewParam)
  self.skewReadout:setAttributes(app.unitNone, skewMap)
  self.skewReadout:setPrecision(2)
  self.skewReadout:setCenter(col2, center4)
  self.paramSubGraphic:addChild(self.skewReadout)
  self.paramSubGraphic:addChild(app.SubButton("skew", 2))

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
end

function ScanSkewControl:setParamMode(enabled)
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

function ScanSkewControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function ScanSkewControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function ScanSkewControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function ScanSkewControl:shiftReleased()
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

function ScanSkewControl:setParamFocusedReadout(readout)
  if readout then readout:save() end
  self.paramFocusedReadout = readout
  self:setSubCursorController(readout)
end

function ScanSkewControl:subReleased(i, shifted)
  if self.paramMode then
    if i == 2 then
      if shifted then
        ShiftHelpers.openKeyboardFor(self.skewReadout, "skew")
      else
        if not self:hasFocus("encoder") then self:focus() end
        self:setParamFocusedReadout(self.skewReadout)
      end
      return true
    end
    return false
  end
  return GainBias.subReleased(self, i, shifted)
end

function ScanSkewControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function ScanSkewControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function ScanSkewControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function ScanSkewControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

function ScanSkewControl:upReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    return true
  end
  return GainBias.upReleased(self, shifted)
end

return ScanSkewControl
