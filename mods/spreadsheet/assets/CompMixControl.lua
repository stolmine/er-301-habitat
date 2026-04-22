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

local CompMixControl = Class {}
CompMixControl:include(GainBias)

function CompMixControl:init(args)
  GainBias.init(self, args)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic
  self.compressor = args.compressor
  self.compressorR = args.compressorR

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Mix", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.paramSubGraphic:addChild(desc)

  -- Auto makeup toggle indicator. Enable serialization on BOTH op options
  -- so the R-side state survives quicksave/reload in stereo.
  local autoOption = args.compressor:getOption("AutoMakeup")
  autoOption:enableSerialization()
  if args.compressorR then
    args.compressorR:getOption("AutoMakeup"):enableSerialization()
  end
  self.autoIndicator = app.BinaryIndicator(0, 24, ply, 32)
  self.autoIndicator:setCenter(col1, center3)
  self.paramSubGraphic:addChild(self.autoIndicator)

  -- Output level readout
  local outputMap = (function()
    local m = app.LinearDialMap(0, 2)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()
  self.outputReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.outputLevel)
    g:setAttributes(app.unitNone, outputMap)
    g:setPrecision(2)
    g:setCenter(col2, center4)
    return g
  end)()
  self.paramSubGraphic:addChild(self.outputReadout)

  self.paramSubGraphic:addChild(app.SubButton("auto", 1))
  self.paramSubGraphic:addChild(app.SubButton("output", 2))
end

function CompMixControl:updateAutoIndicator()
  if self.compressor and self.compressor:isAutoMakeupEnabled() then
    self.autoIndicator:on()
  else
    self.autoIndicator:off()
  end
end

function CompMixControl:setParamMode(enabled)
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

function CompMixControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function CompMixControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function CompMixControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function CompMixControl:shiftReleased()
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

function CompMixControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function CompMixControl:subReleased(i, shifted)
  if shifted then return false end
  if self.paramMode then
    if i == 1 then
      self.compressor:toggleAutoMakeup()
      if self.compressorR
          and self.compressorR:isAutoMakeupEnabled() ~= self.compressor:isAutoMakeupEnabled() then
        self.compressorR:toggleAutoMakeup()
      end
      self:updateAutoIndicator()
    elseif i == 2 then
      self.outputReadout:save()
      self.paramFocusedReadout = self.outputReadout
      self:setSubCursorController(self.outputReadout)
      if not self:hasFocus("encoder") then self:focus() end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function CompMixControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function CompMixControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function CompMixControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return CompMixControl
