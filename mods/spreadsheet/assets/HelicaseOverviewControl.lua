local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
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

local HelicaseOverviewControl = Class {}
HelicaseOverviewControl:include(GainBias)

function HelicaseOverviewControl:init(args)
  GainBias.init(self, args)

  -- Replace fader with phase space viz
  local phase = libspreadsheet.HelicasePhaseGraphic(0, 0, ply, 64)
  phase:follow(args.helicase)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(phase)
  self:setMainCursorController(phase)
  self:setControlGraphic(container)

  -- Default sub-display: modMix, lin/expo, (slot 3 free)
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

  local mixMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  self.mixReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.gainbias:getParameter("Bias"))
    g:setAttributes(app.unitNone, mixMap)
    g:setPrecision(2)
    g:setCenter(col1, center4)
    return g
  end)()
  self.paramSubGraphic:addChild(self.mixReadout)

  -- Lin/Expo label
  self.helicase = args.helicase
  local linOption = args.helicase:getOption("LinExpo")
  linOption:enableSerialization()
  self.linExpoLabel = app.Label("exp", 10)
  self.linExpoLabel:fitToText(0)
  self.linExpoLabel:setCenter(col2, center3 + 1)
  self.paramSubGraphic:addChild(self.linExpoLabel)

  -- Carrier shape readout
  local shapeMap = (function()
    local m = app.LinearDialMap(0, 7)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()
  self.carrierShapeParam = args.carrierShapeParam
  self.shapeReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.carrierShapeParam)
    g:setAttributes(app.unitNone, shapeMap)
    g:setPrecision(0)
    g:setCenter(col3, center4)
    return g
  end)()
  self.paramSubGraphic:addChild(self.shapeReadout)

  self.paramSubGraphic:addChild(app.SubButton("mix", 1))
  self.paramSubGraphic:addChild(app.SubButton("l/e", 2))
  self.paramSubGraphic:addChild(app.SubButton("carr", 3))

  self:setParamMode(true)
end

function HelicaseOverviewControl:updateLinExpo()
  if self.helicase:isLinFM() then
    self.linExpoLabel:setText("lin")
  else
    self.linExpoLabel:setText("exp")
  end
end

function HelicaseOverviewControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.paramSubGraphic
    self:updateLinExpo()
  else
    self.subGraphic = self.levelSubGraphic
    self:setFocusedReadout(self.bias)
  end
  self:addSubGraphic(self.subGraphic)
end

function HelicaseOverviewControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function HelicaseOverviewControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function HelicaseOverviewControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function HelicaseOverviewControl:shiftReleased()
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

function HelicaseOverviewControl:subReleased(i, shifted)
  if shifted then return false end
  if self.paramMode then
    if i == 1 then
      self.mixReadout:save()
      self.paramFocusedReadout = self.mixReadout
      self:setSubCursorController(self.mixReadout)
      if not self:hasFocus("encoder") then self:focus() end
    elseif i == 2 then
      -- Toggle lin/expo
      self.helicase:toggleLinFM()
      self:updateLinExpo()
    elseif i == 3 then
      self.shapeReadout:save()
      self.paramFocusedReadout = self.shapeReadout
      self:setSubCursorController(self.shapeReadout)
      if not self:hasFocus("encoder") then self:focus() end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function HelicaseOverviewControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function HelicaseOverviewControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function HelicaseOverviewControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return HelicaseOverviewControl
