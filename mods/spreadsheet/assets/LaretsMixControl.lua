local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center3 = app.GRID5_CENTER3
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local LaretsMixControl = Class {}
LaretsMixControl:include(GainBias)

function LaretsMixControl:init(args)
  GainBias.init(self, args)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic
  self.op = args.op

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local function makeReadout(param, map, precision, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(app.unitNone, map)
    g:setPrecision(precision)
    g:setCenter(x, center4)
    return g
  end

  local levelMap = (function()
    local m = app.LinearDialMap(0, 4)
    m:setSteps(0.5, 0.1, 0.01, 0.001)
    return m
  end)()

  local compMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  self.outputReadout = makeReadout(args.outputLevel, levelMap, 2, col1)
  self.compReadout = makeReadout(args.compressAmt, compMap, 2, col2)

  local autoOption = args.op:getOption("AutoMakeup")
  autoOption:enableSerialization()
  self.autoIndicator = app.BinaryIndicator(0, 24, ply, 32)
  self.autoIndicator:setCenter(col3, center3)

  local desc = app.Label("Output / Comp / Auto", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  self.paramSubGraphic:addChild(self.outputReadout)
  self.paramSubGraphic:addChild(self.compReadout)
  self.paramSubGraphic:addChild(self.autoIndicator)
  self.paramSubGraphic:addChild(desc)
  self.paramSubGraphic:addChild(app.SubButton("out", 1))
  self.paramSubGraphic:addChild(app.SubButton("comp", 2))
  self.paramSubGraphic:addChild(app.SubButton("auto", 3))
end

function LaretsMixControl:updateAutoIndicator()
  if self.op and self.op:isAutoMakeupEnabled() then
    self.autoIndicator:on()
  else
    self.autoIndicator:off()
  end
end

function LaretsMixControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.paramSubGraphic
    self:updateAutoIndicator()
  else
    self.subGraphic = self.normalSubGraphic
    self:setFocusedReadout(self.bias)
  end
  self:addSubGraphic(self.subGraphic)
end

function LaretsMixControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function LaretsMixControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function LaretsMixControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function LaretsMixControl:shiftReleased()
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

function LaretsMixControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function LaretsMixControl:subReleased(i, shifted)
  if shifted then return false end
  if self.paramMode then
    if i == 1 then
      self.outputReadout:save()
      self.paramFocusedReadout = self.outputReadout
      self:setSubCursorController(self.outputReadout)
      if not self:hasFocus("encoder") then self:focus() end
    elseif i == 2 then
      self.compReadout:save()
      self.paramFocusedReadout = self.compReadout
      self:setSubCursorController(self.compReadout)
      if not self:hasFocus("encoder") then self:focus() end
    elseif i == 3 then
      self.op:toggleAutoMakeup()
      self:updateAutoIndicator()
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function LaretsMixControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function LaretsMixControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function LaretsMixControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return LaretsMixControl
