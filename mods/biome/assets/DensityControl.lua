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

local DensityControl = Class {}
DensityControl:include(GainBias)

function DensityControl:init(args)
  GainBias.init(self, args)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local patternMap = (function()
    local m = app.LinearDialMap(0, 15)
    m:setSteps(4, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  local slopeMap = (function()
    local m = app.LinearDialMap(0, 3)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  local resonatorMap = (function()
    local m = app.LinearDialMap(0, 3)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
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

  self.patternReadout = makeReadout(args.pattern, patternMap, 0, col1)
  if self.patternReadout.addName then
    for _, v in ipairs({"unif", "fib", "early", "late", "mid", "ess", "flat", "rfib",
                        "r.un", "r.fi", "r.ea", "r.la", "r.mi", "r.es", "r.fl", "r.rf"}) do
      self.patternReadout:addName(v)
    end
  end

  self.slopeReadout = makeReadout(args.slope, slopeMap, 0, col2)
  if self.slopeReadout.addName then
    for _, v in ipairs({"flat", "rise", "fall", "hump"}) do
      self.slopeReadout:addName(v)
    end
  end

  self.resonatorReadout = makeReadout(args.resonator, resonatorMap, 0, col3)
  if self.resonatorReadout.addName then
    for _, v in ipairs({"raw", "gtr", "clar", "sitr"}) do
      self.resonatorReadout:addName(v)
    end
  end

  local desc = app.Label("Patt / Slope / Res", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  self.paramSubGraphic:addChild(self.patternReadout)
  self.paramSubGraphic:addChild(self.slopeReadout)
  self.paramSubGraphic:addChild(self.resonatorReadout)
  self.paramSubGraphic:addChild(desc)
  self.paramSubGraphic:addChild(app.SubButton("patt", 1))
  self.paramSubGraphic:addChild(app.SubButton("slope", 2))
  self.paramSubGraphic:addChild(app.SubButton("res", 3))
end

function DensityControl:setParamMode(enabled)
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

function DensityControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function DensityControl:onCursorLeave(spot)
  if self.paramMode then
    self:removeSubGraphic(self.subGraphic)
    self.paramMode = false
    self.subGraphic = self.normalSubGraphic
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function DensityControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function DensityControl:shiftReleased()
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

function DensityControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function DensityControl:subReleased(i, shifted)
  if shifted then return false end
  if self.paramMode then
    local readout = i == 1 and self.patternReadout
        or i == 2 and self.slopeReadout
        or i == 3 and self.resonatorReadout or nil
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

function DensityControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function DensityControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function DensityControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return DensityControl
