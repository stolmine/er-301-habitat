local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local LaretOverviewControl = Class {}
LaretOverviewControl:include(GainBias)

function LaretOverviewControl:init(args)
  GainBias.init(self, args)

  local overview = libspreadsheet.LaretOverviewGraphic(0, 0, ply * 2, 64)
  overview:follow(args.op)
  local container = app.Graphic(0, 0, ply * 2, 64)
  container:addChild(overview)
  self:setMainCursorController(overview)
  self:setControlGraphic(container)
  self:addSpotDescriptor { center = ply }

  self.paramMode = true
  self.shiftHeld = false
  self.shiftUsed = false
  self.levelSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Overview", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.paramSubGraphic:addChild(desc)

  local skewMap = (function()
    local m = app.LinearDialMap(-1, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  local stepCountMap = (function()
    local m = app.LinearDialMap(1, 16)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  local loopMap = (function()
    local m = app.LinearDialMap(0, 16)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  self.skewReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.gainbias:getParameter("Bias"))
    g:setAttributes(app.unitNone, skewMap)
    g:setPrecision(2)
    g:setCenter(col1, center4)
    return g
  end)()

  self.stepCountReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.stepCountParam)
    g:setAttributes(app.unitNone, stepCountMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    return g
  end)()

  self.loopReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.loopParam)
    g:setAttributes(app.unitNone, loopMap)
    g:setPrecision(0)
    g:setCenter(col3, center4)
    return g
  end)()
  if self.loopReadout.addName then
    self.loopReadout:addName("all")
    for i = 1, 16 do
      self.loopReadout:addName(tostring(i))
    end
  end

  self.paramSubGraphic:addChild(self.skewReadout)
  self.paramSubGraphic:addChild(self.stepCountReadout)
  self.paramSubGraphic:addChild(self.loopReadout)
  self.paramSubGraphic:addChild(app.SubButton("skew", 1))
  self.paramSubGraphic:addChild(app.SubButton("steps", 2))
  self.paramSubGraphic:addChild(app.SubButton("loop", 3))

  self:setParamMode(true)
end

function LaretOverviewControl:setParamMode(enabled)
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

function LaretOverviewControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function LaretOverviewControl:onCursorLeave(spot)
  if not self.paramMode then
    self:removeSubGraphic(self.subGraphic)
    self.paramMode = true
    self.subGraphic = self.paramSubGraphic
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function LaretOverviewControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function LaretOverviewControl:shiftReleased()
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

function LaretOverviewControl:focusParamReadout(readout)
  readout:save()
  self.paramFocusedReadout = readout
  self:setSubCursorController(readout)
  if not self:hasFocus("encoder") then self:focus() end
end

function LaretOverviewControl:subReleased(i, shifted)
  if shifted then return false end
  if self.paramMode then
    if i == 1 then self:focusParamReadout(self.skewReadout)
    elseif i == 2 then self:focusParamReadout(self.stepCountReadout)
    elseif i == 3 then self:focusParamReadout(self.loopReadout)
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function LaretOverviewControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function LaretOverviewControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function LaretOverviewControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return LaretOverviewControl
