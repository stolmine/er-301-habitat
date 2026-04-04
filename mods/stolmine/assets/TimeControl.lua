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

local TimeControl = Class {}
TimeControl:include(GainBias)

function TimeControl:init(args)
  GainBias.init(self, args)

  self.paramMode = false
  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local function makeReadout(param, map, precision, units, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(units, map)
    g:setPrecision(precision)
    g:setCenter(x, center4)
    return g
  end

  local gridMap = (function()
    local m = app.LinearDialMap(0, 4)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  local reverseMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  local skewMap = (function()
    local m = app.LinearDialMap(-2, 2)
    m:setSteps(0.5, 0.1, 0.01, 0.001)
    return m
  end)()

  self.gridReadout = makeReadout(args.grid, gridMap, 0, app.unitNone, col1)
  if self.gridReadout.addName then
    for _, v in ipairs({"1", "2", "4", "8", "16"}) do
      self.gridReadout:addName(v)
    end
  end

  self.reverseReadout = makeReadout(args.reverse, reverseMap, 2, app.unitNone, col2)
  self.skewReadout = makeReadout(args.skew, skewMap, 2, app.unitNone, col3)

  local desc = app.Label("Grid / Rev / Skew", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  self.paramSubGraphic:addChild(self.gridReadout)
  self.paramSubGraphic:addChild(self.reverseReadout)
  self.paramSubGraphic:addChild(self.skewReadout)
  self.paramSubGraphic:addChild(desc)
  self.paramSubGraphic:addChild(app.SubButton("grid", 1))
  self.paramSubGraphic:addChild(app.SubButton("rev", 2))
  self.paramSubGraphic:addChild(app.SubButton("skew", 3))
end

function TimeControl:setParamMode(enabled)
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

function TimeControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function TimeControl:onCursorLeave(spot)
  if self.paramMode then
    self:removeSubGraphic(self.subGraphic)
    self.paramMode = false
    self.subGraphic = self.normalSubGraphic
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function TimeControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function TimeControl:shiftReleased()
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

function TimeControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function TimeControl:subReleased(i, shifted)
  if shifted then return false end
  if self.paramMode then
    local readout = i == 1 and self.gridReadout
        or i == 2 and self.reverseReadout
        or i == 3 and self.skewReadout or nil
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

function TimeControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Coarse)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function TimeControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function TimeControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return TimeControl
