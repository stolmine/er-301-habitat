-- FocusShapeControl -- Blanda's Focus ply with per-input Shape params
-- accessible on shift. GainBias-based: main-view encoder edits the
-- global Focus bias by default. Shift-toggle reveals three Shape
-- readouts (one per input) with sub-buttons to focus each.

local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local FocusShapeControl = Class {}
FocusShapeControl:include(GainBias)

function FocusShapeControl:init(args)
  GainBias.init(self, args)

  self:setClassName("FocusShapeControl")

  self.defaultSubGraphic = self.subGraphic

  -- Shape sub-display.
  self.shapeSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Bell Shape", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.shapeSubGraphic:addChild(desc)

  local shapeMap = args.shapeMap or (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.0001)
    return m
  end)()

  local function makeReadout(param, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(app.unitNone, shapeMap)
    g:setPrecision(2)
    g:setCenter(x, center4)
    return g
  end

  self.shape0Readout = makeReadout(args.shape0Param, col1)
  self.shape1Readout = makeReadout(args.shape1Param, col2)
  self.shape2Readout = makeReadout(args.shape2Param, col3)
  self.shapeSubGraphic:addChild(self.shape0Readout)
  self.shapeSubGraphic:addChild(self.shape1Readout)
  self.shapeSubGraphic:addChild(self.shape2Readout)
  self.shapeSubGraphic:addChild(app.SubButton("in1", 1))
  self.shapeSubGraphic:addChild(app.SubButton("in2", 2))
  self.shapeSubGraphic:addChild(app.SubButton("in3", 3))

  self.focusMode = true
  self.shiftHeld = false
  self.shiftUsed = false
end

function FocusShapeControl:setFocusMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.focusMode = enabled
  self.shapeFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.defaultSubGraphic
    self:setFocusedReadout(self.bias)
  else
    self.subGraphic = self.shapeSubGraphic
  end
  self:addSubGraphic(self.subGraphic)
end

function FocusShapeControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function FocusShapeControl:onCursorLeave(spot)
  if not self.focusMode then
    self:removeSubGraphic(self.subGraphic)
    self.focusMode = true
    self.subGraphic = self.defaultSubGraphic
    self:addSubGraphic(self.subGraphic)
    self.shapeFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function FocusShapeControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.shapeFocusedReadout then
    self.shiftSnapshot = self.shapeFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function FocusShapeControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.shapeFocusedReadout and self.shiftSnapshot then
      local cur = self.shapeFocusedReadout:getValueInUnits()
      if cur ~= self.shiftSnapshot then
        self.shiftHeld = false
        self.shiftSnapshot = nil
        return true
      end
    end
    self:setFocusMode(not self.focusMode)
  end
  self.shiftHeld = false
  self.shiftSnapshot = nil
  return true
end

function FocusShapeControl:setShapeFocusedReadout(readout)
  if readout then readout:save() end
  self.shapeFocusedReadout = readout
  self:setSubCursorController(readout)
end

function FocusShapeControl:subReleased(i, shifted)
  if shifted then return false end
  if self.focusMode then
    return GainBias.subReleased(self, i, shifted)
  else
    local r = (i == 1) and self.shape0Readout
          or  (i == 2) and self.shape1Readout
          or  (i == 3) and self.shape2Readout
    if r then
      if not self:hasFocus("encoder") then self:focus() end
      self:setShapeFocusedReadout(r)
      return true
    end
  end
  return false
end

function FocusShapeControl:spotReleased(spot, shifted)
  if not self.focusMode then
    self.shapeFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setFocusMode(true)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function FocusShapeControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.shapeFocusedReadout then
    self.shapeFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function FocusShapeControl:zeroPressed()
  if self.shapeFocusedReadout then
    self.shapeFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function FocusShapeControl:cancelReleased(shifted)
  if self.shapeFocusedReadout then
    self.shapeFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

function FocusShapeControl:upReleased(shifted)
  if self.shapeFocusedReadout then
    self.shapeFocusedReadout = nil
    self:setSubCursorController(nil)
    return true
  end
  return GainBias.upReleased(self, shifted)
end

return FocusShapeControl
