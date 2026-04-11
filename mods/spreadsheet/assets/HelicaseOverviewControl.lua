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

  -- Lin/Expo indicator
  self.linExpoParam = args.linExpoParam
  self.linExpoIndicator = app.BinaryIndicator(0, 24, ply, 32)
  self.linExpoIndicator:setCenter(col2, center3)
  self.paramSubGraphic:addChild(self.linExpoIndicator)

  self.paramSubGraphic:addChild(app.SubButton("mix", 1))
  self.paramSubGraphic:addChild(app.SubButton("l/e", 2))

  self:setParamMode(true)
end

function HelicaseOverviewControl:updateLinExpo()
  if self.linExpoParam and self.linExpoParam:target() > 0.5 then
    self.linExpoIndicator:on()
  else
    self.linExpoIndicator:off()
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
  if not self.paramMode then
    self:removeSubGraphic(self.subGraphic)
    self.paramMode = true
    self.subGraphic = self.paramSubGraphic
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function HelicaseOverviewControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  return true
end

function HelicaseOverviewControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    self:setParamMode(not self.paramMode)
  end
  self.shiftHeld = false
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
      if self.linExpoParam then
        local cur = self.linExpoParam:target()
        self.linExpoParam:hardSet(cur > 0.5 and 0.0 or 1.0)
        self:updateLinExpo()
      end
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
