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

local HelicaseShapingControl = Class {}
HelicaseShapingControl:include(GainBias)

function HelicaseShapingControl:init(args)
  GainBias.init(self, args)

  -- Replace fader with transfer curve viz
  self.curveGraphic = libspreadsheet.HelicaseCurveGraphic(0, 0, ply, 64)
  self.curveGraphic:follow(args.helicase)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(self.curveGraphic)
  self:setMainCursorController(self.curveGraphic)
  self:setControlGraphic(container)

  self.discIndexParam = args.discIndexParam
  self.discTypeParam = args.discTypeParam

  -- Default sub-display: modIndex, discIndex, discType
  self.paramMode = true
  self.shiftHeld = false
  self.shiftUsed = false
  self.levelSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Shaping", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.paramSubGraphic:addChild(desc)

  local function makeReadout(param, map, precision, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(app.unitNone, map)
    g:setPrecision(precision)
    g:setCenter(x, center4)
    return g
  end

  local indexMap = (function()
    local m = app.LinearDialMap(0, 10)
    m:setSteps(1, 0.1, 0.01, 0.001)
    return m
  end)()
  local discMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()
  local typeMap = (function()
    local m = app.LinearDialMap(0, 15)
    m:setSteps(1, 0.5, 0.1, 0.01)
    return m
  end)()

  self.indexReadout = makeReadout(args.gainbias:getParameter("Bias"), indexMap, 2, col1)
  self.discReadout = makeReadout(args.discIndexParam, discMap, 2, col2)
  self.typeReadout = makeReadout(args.discTypeParam, typeMap, 1, col3)

  self.paramSubGraphic:addChild(self.indexReadout)
  self.paramSubGraphic:addChild(self.discReadout)
  self.paramSubGraphic:addChild(self.typeReadout)
  self.paramSubGraphic:addChild(app.SubButton("index", 1))
  self.paramSubGraphic:addChild(app.SubButton("disc", 2))
  self.paramSubGraphic:addChild(app.SubButton("type", 3))

  self:setParamMode(true)
end

function HelicaseShapingControl:setParamMode(enabled)
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

function HelicaseShapingControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  end

function HelicaseShapingControl:onCursorLeave(spot)
  if not self.paramMode then
    self:removeSubGraphic(self.subGraphic)
    self.paramMode = true
    self.subGraphic = self.paramSubGraphic
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function HelicaseShapingControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  return true
end

function HelicaseShapingControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    self:setParamMode(not self.paramMode)
  end
  self.shiftHeld = false
  return true
end

function HelicaseShapingControl:subReleased(i, shifted)
  if shifted then return false end
  if self.paramMode then
    local readout = nil
    if i == 1 then readout = self.indexReadout
    elseif i == 2 then readout = self.discReadout
    elseif i == 3 then readout = self.typeReadout
    end
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

function HelicaseShapingControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
        return true
  end
  local result = GainBias.encoder(self, change, shifted)
    return result
end

function HelicaseShapingControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
        return true
  end
  return GainBias.zeroPressed(self)
end

function HelicaseShapingControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
        return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return HelicaseShapingControl
