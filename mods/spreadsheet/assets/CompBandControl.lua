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

local CompBandControl = Class {}
CompBandControl:include(GainBias)

function CompBandControl:init(args)
  GainBias.init(self, args)

  -- Replace fader with spectrum display
  local spectrum = libspreadsheet.CompressorSpectrumGraphic(0, 0, ply, 64)
  spectrum:follow(args.compressor)
  spectrum:setBandIndex(args.bandIndex or 0)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(spectrum)
  self:setMainCursorController(spectrum)
  self:setControlGraphic(container)

  -- Default sub-display = comp params (threshold/ratio/speed)
  -- Shift toggles to normal GainBias sub-display (gain/bias for level).
  -- Init preserved per Decision 7 grandfather list: paramMode=true
  -- (custom comp params) is the default view on insert.
  self.paramMode = true
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic -- save the GainBias sub-display

  -- Build comp params sub-display
  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label(args.description or "Band", 10)
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

  local threshMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()
  local ratioMap = (function()
    local m = app.LinearDialMap(1, 20)
    m:setSteps(1, 0.5, 0.1, 0.1)
    return m
  end)()
  local speedMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  self.threshReadout = makeReadout(args.thresholdParam, threshMap, 2, col1)
  self.ratioReadout = makeReadout(args.ratioParam, ratioMap, 1, col2)
  self.speedReadout = makeReadout(args.speedParam, speedMap, 2, col3)

  self.paramSubGraphic:addChild(self.threshReadout)
  self.paramSubGraphic:addChild(self.ratioReadout)
  self.paramSubGraphic:addChild(self.speedReadout)
  self.paramSubGraphic:addChild(app.SubButton("thresh", 1))
  self.paramSubGraphic:addChild(app.SubButton("ratio", 2))
  self.paramSubGraphic:addChild(app.SubButton("speed", 3))

  -- Start in paramMode (custom comp params visible).
  self:setParamMode(true)
end

function CompBandControl:setParamMode(enabled)
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

function CompBandControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function CompBandControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function CompBandControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function CompBandControl:shiftReleased()
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

function CompBandControl:setParamFocusedReadout(readout)
  if readout then readout:save() end
  self.paramFocusedReadout = readout
  self:setSubCursorController(readout)
end

function CompBandControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function CompBandControl:subReleased(i, shifted)
  if self.paramMode then
    local readout, label
    if i == 1 then readout, label = self.threshReadout, "threshold"
    elseif i == 2 then readout, label = self.ratioReadout, "ratio"
    elseif i == 3 then readout, label = self.speedReadout, "speed"
    end
    if readout then
      if shifted then
        ShiftHelpers.openKeyboardFor(readout, label)
      else
        self:setParamFocusedReadout(readout)
        if not self:hasFocus("encoder") then self:focus() end
      end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function CompBandControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function CompBandControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function CompBandControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return CompBandControl
