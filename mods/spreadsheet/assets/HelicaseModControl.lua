local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
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

local HelicaseModControl = Class {}
HelicaseModControl:include(GainBias)

function HelicaseModControl:init(args)
  GainBias.init(self, args)

  -- Replace fader with orbital viz
  local orbital = libspreadsheet.HelicaseOrbitalGraphic(0, 0, ply, 64)
  orbital:follow(args.helicase)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(orbital)
  self:setMainCursorController(orbital)
  self:setControlGraphic(container)

  -- Default sub-display: ratio, feedback, modShape
  self.paramMode = true
  self.shiftHeld = false
  self.shiftUsed = false
  self.levelSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Modulator", 10)
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

  local ratioMap = (function()
    local m = app.LinearDialMap(0.5, 16)
    m:setSteps(1, 0.5, 0.1, 0.01)
    return m
  end)()
  local fbMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()
  local shapeMap = (function()
    local m = app.LinearDialMap(0, 7)
    m:setSteps(1, 0.1, 0.01, 0.001)
    return m
  end)()

  self.ratioReadout = makeReadout(args.gainbias:getParameter("Bias"), ratioMap, 2, col1)
  self.fbReadout = makeReadout(args.feedbackParam, fbMap, 2, col2)
  self.shapeReadout = makeReadout(args.modShapeParam, shapeMap, 0, col3)

  self.paramSubGraphic:addChild(self.ratioReadout)
  self.paramSubGraphic:addChild(self.fbReadout)
  self.paramSubGraphic:addChild(self.shapeReadout)
  self.paramSubGraphic:addChild(app.SubButton("ratio", 1))
  self.paramSubGraphic:addChild(app.SubButton("fdbk", 2))
  self.paramSubGraphic:addChild(app.SubButton("shape", 3))

  self:setParamMode(true)
end

function HelicaseModControl:setParamMode(enabled)
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

function HelicaseModControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function HelicaseModControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function HelicaseModControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function HelicaseModControl:shiftReleased()
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

function HelicaseModControl:subReleased(i, shifted)
  if self.paramMode then
    local readout, label
    if i == 1 then readout, label = self.ratioReadout, "ratio"
    elseif i == 2 then readout, label = self.fbReadout, "feedback"
    elseif i == 3 then readout, label = self.shapeReadout, "shape"
    end
    if readout then
      if shifted then
        ShiftHelpers.openKeyboardFor(readout, label)
      else
        readout:save()
        self.paramFocusedReadout = readout
        self:setSubCursorController(readout)
        if not self:hasFocus("encoder") then self:focus() end
      end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function HelicaseModControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function HelicaseModControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function HelicaseModControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return HelicaseModControl
