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

local BBCutDensityControl = Class {}
BBCutDensityControl:include(GainBias)

function BBCutDensityControl:init(args)
  GainBias.init(self, args)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local stutterAreaMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  local repeatsMap = (function()
    local m = app.LinearDialMap(0, 8)
    m:setSteps(1, 0.5, 0.1, 0.01)
    return m
  end)()

  local accelMap = (function()
    local m = app.LinearDialMap(0.5, 0.999)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  local function makeReadout(param, map, precision, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(app.unitNone, map)
    g:setPrecision(precision)
    g:setCenter(x, center4)
    return g
  end

  self.stutterAreaReadout = makeReadout(args.stutterArea, stutterAreaMap, 2, col1)
  self.repeatsReadout = makeReadout(args.repeats, repeatsMap, 1, col2)
  self.accelReadout = makeReadout(args.accel, accelMap, 3, col3)

  local desc = app.Label("Stut / Rep / Accel", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  self.paramSubGraphic:addChild(self.stutterAreaReadout)
  self.paramSubGraphic:addChild(self.repeatsReadout)
  self.paramSubGraphic:addChild(self.accelReadout)
  self.paramSubGraphic:addChild(desc)
  self.paramSubGraphic:addChild(app.SubButton("stut", 1))
  self.paramSubGraphic:addChild(app.SubButton("rep", 2))
  self.paramSubGraphic:addChild(app.SubButton("accel", 3))
end

function BBCutDensityControl:setParamMode(enabled)
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

function BBCutDensityControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function BBCutDensityControl:onCursorLeave(spot)
  if self.paramMode then
    self:removeSubGraphic(self.subGraphic)
    self.paramMode = false
    self.subGraphic = self.normalSubGraphic
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function BBCutDensityControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function BBCutDensityControl:shiftReleased()
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

function BBCutDensityControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function BBCutDensityControl:subReleased(i, shifted)
  if shifted then return false end
  if self.paramMode then
    local readout = i == 1 and self.stutterAreaReadout
        or i == 2 and self.repeatsReadout
        or i == 3 and self.accelReadout or nil
    if readout then
      readout:save()
      self.paramFocusedReadout = readout
      self:setSubCursorController(readout)
      if not self:hasFocus("encoder") then self:focus() end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function BBCutDensityControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function BBCutDensityControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function BBCutDensityControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return BBCutDensityControl
