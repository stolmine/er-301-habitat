-- ScanSkewControl -- Blanda's Scan ply with the global Skew macro
-- accessible on shift. GainBias-based: main-view encoder edits the
-- global Scan bias by default. Shift-toggle reveals a single centered
-- Skew readout with a sub-button to focus it.

local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col2 = app.BUTTON2_CENTER

local ScanSkewControl = Class {}
ScanSkewControl:include(GainBias)

function ScanSkewControl:init(args)
  GainBias.init(self, args)

  self:setClassName("ScanSkewControl")

  self.defaultSubGraphic = self.subGraphic

  -- Skew sub-display.
  self.skewSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Skew", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.skewSubGraphic:addChild(desc)

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
  self.skewSubGraphic:addChild(self.skewReadout)
  self.skewSubGraphic:addChild(app.SubButton("skew", 2))

  self.scanMode = true
  self.shiftHeld = false
  self.shiftUsed = false

  -- Defensive: keep focusedReadout pointing at Scan's bias via the
  -- proper method so the sub cursor controller and encoder state are
  -- installed too. GainBias.init already did this at line 197 of
  -- GainBias.lua; redundant but explicit. See
  -- feedback_gainbias_dual_mode_focus.md.
  self:setFocusedReadout(self.bias)
end

function ScanSkewControl:setScanMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.scanMode = enabled
  self.skewFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.defaultSubGraphic
    self:setFocusedReadout(self.bias)
  else
    self.subGraphic = self.skewSubGraphic
  end
  self:addSubGraphic(self.subGraphic)
end

function ScanSkewControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function ScanSkewControl:onCursorLeave(spot)
  if not self.scanMode then
    self:removeSubGraphic(self.subGraphic)
    self.scanMode = true
    self.subGraphic = self.defaultSubGraphic
    self:addSubGraphic(self.subGraphic)
    self.skewFocusedReadout = nil
    self:setFocusedReadout(self.bias)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function ScanSkewControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.skewFocusedReadout then
    self.shiftSnapshot = self.skewFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function ScanSkewControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.skewFocusedReadout and self.shiftSnapshot then
      local cur = self.skewFocusedReadout:getValueInUnits()
      if cur ~= self.shiftSnapshot then
        self.shiftHeld = false
        self.shiftSnapshot = nil
        return true
      end
    end
    self:setScanMode(not self.scanMode)
  end
  self.shiftHeld = false
  self.shiftSnapshot = nil
  return true
end

function ScanSkewControl:setSkewFocusedReadout(readout)
  if readout then readout:save() end
  self.skewFocusedReadout = readout
  self:setSubCursorController(readout)
end

function ScanSkewControl:subReleased(i, shifted)
  if shifted then return false end
  if self.scanMode then
    return GainBias.subReleased(self, i, shifted)
  else
    if i == 2 then
      if not self:hasFocus("encoder") then self:focus() end
      self:setSkewFocusedReadout(self.skewReadout)
      return true
    end
  end
  return false
end

function ScanSkewControl:spotReleased(spot, shifted)
  if not self.scanMode then
    self.skewFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setScanMode(true)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function ScanSkewControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.skewFocusedReadout then
    self.skewFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function ScanSkewControl:zeroPressed()
  if self.skewFocusedReadout then
    self.skewFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function ScanSkewControl:cancelReleased(shifted)
  if self.skewFocusedReadout then
    self.skewFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

function ScanSkewControl:upReleased(shifted)
  if self.skewFocusedReadout then
    self.skewFocusedReadout = nil
    self:setSubCursorController(nil)
    return true
  end
  return GainBias.upReleased(self, shifted)
end

return ScanSkewControl
