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

local SomModControl = Class {}
SomModControl:include(GainBias)

function SomModControl:init(args)
  GainBias.init(self, args)

  self.paramMode = true
  self.shiftHeld = false
  self.shiftUsed = false
  self.shiftSnapshot = nil
  self.levelSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Rate / Shape / FB", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.paramSubGraphic:addChild(desc)

  local rateMap = (function()
    local m = app.LinearDialMap(0.001, 20)
    m:setSteps(1, 0.1, 0.01, 0.001)
    return m
  end)()
  local shapeMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()
  local fbMap = (function()
    local m = app.LinearDialMap(0, 0.95)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  self.rateReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.rateParam)
    g:setAttributes(app.unitHertz, rateMap)
    g:setPrecision(2)
    g:setCenter(col1, center4)
    return g
  end)()

  self.shapeReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.shapeParam)
    g:setAttributes(app.unitNone, shapeMap)
    g:setPrecision(2)
    g:setCenter(col2, center4)
    return g
  end)()

  self.fbReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.fbParam)
    g:setAttributes(app.unitNone, fbMap)
    g:setPrecision(2)
    g:setCenter(col3, center4)
    return g
  end)()

  self.paramSubGraphic:addChild(self.rateReadout)
  self.paramSubGraphic:addChild(self.shapeReadout)
  self.paramSubGraphic:addChild(self.fbReadout)
  self.paramSubGraphic:addChild(app.SubButton("rate", 1))
  self.paramSubGraphic:addChild(app.SubButton("shape", 2))
  self.paramSubGraphic:addChild(app.SubButton("fb", 3))

  self:setParamMode(true)
end

function SomModControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.paramSubGraphic
  else
    self.subGraphic = self.levelSubGraphic
    self:setFocusedReadout(self.bias)
  end
  self:addSubGraphic(self.subGraphic)
end

function SomModControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function SomModControl:onCursorLeave(spot)
  if not self.paramMode then
    self:removeSubGraphic(self.subGraphic)
    self.paramMode = true
    self.subGraphic = self.paramSubGraphic
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function SomModControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function SomModControl:shiftReleased()
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

function SomModControl:focusParamReadout(readout)
  readout:save()
  self.paramFocusedReadout = readout
  self:setSubCursorController(readout)
  if not self:hasFocus("encoder") then self:focus() end
end

function SomModControl:subReleased(i, shifted)
  if shifted then return false end
  if self.paramMode then
    if i == 1 then self:focusParamReadout(self.rateReadout)
    elseif i == 2 then self:focusParamReadout(self.shapeReadout)
    elseif i == 3 then self:focusParamReadout(self.fbReadout)
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function SomModControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function SomModControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function SomModControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return SomModControl
