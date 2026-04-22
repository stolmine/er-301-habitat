-- FocusShapeControl -- Blanda's Focus ply with per-input Shape params
-- accessible on shift. GainBias-based: main-view encoder edits the
-- global Focus bias by default. Shift-toggle reveals three Shape
-- readouts (one per input) with sub-buttons to focus each.

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
local col3 = app.BUTTON3_CENTER

local FocusShapeControl = Class {}
FocusShapeControl:include(GainBias)

function FocusShapeControl:init(args)
  GainBias.init(self, args)

  self:setClassName("FocusShapeControl")

  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Bell Shape", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.paramSubGraphic:addChild(desc)

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
  self.paramSubGraphic:addChild(self.shape0Readout)
  self.paramSubGraphic:addChild(self.shape1Readout)
  self.paramSubGraphic:addChild(self.shape2Readout)
  self.paramSubGraphic:addChild(app.SubButton("in1", 1))
  self.paramSubGraphic:addChild(app.SubButton("in2", 2))
  self.paramSubGraphic:addChild(app.SubButton("in3", 3))

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
end

function FocusShapeControl:setParamMode(enabled)
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

function FocusShapeControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    self:setSubCursorController(self.paramModeDefaultSub)
  end
end

function FocusShapeControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function FocusShapeControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function FocusShapeControl:shiftReleased()
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

function FocusShapeControl:setParamFocusedReadout(readout)
  if readout then readout:save() end
  self.paramFocusedReadout = readout
  self:setSubCursorController(readout)
end

function FocusShapeControl:subReleased(i, shifted)
  if self.paramMode then
    local r, label
    if i == 1 then r, label = self.shape0Readout, "shape 1"
    elseif i == 2 then r, label = self.shape1Readout, "shape 2"
    elseif i == 3 then r, label = self.shape2Readout, "shape 3"
    end
    if r then
      if shifted then
        ShiftHelpers.openKeyboardFor(r, label)
      else
        if not self:hasFocus("encoder") then self:focus() end
        self:setParamFocusedReadout(r)
      end
      return true
    end
    return false
  end
  return GainBias.subReleased(self, i, shifted)
end

function FocusShapeControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function FocusShapeControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function FocusShapeControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function FocusShapeControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

function FocusShapeControl:upReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    return true
  end
  return GainBias.upReleased(self, shifted)
end

return FocusShapeControl
