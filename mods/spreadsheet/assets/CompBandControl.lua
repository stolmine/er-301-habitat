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
  -- Shift toggles to normal GainBias sub-display (gain/bias for level)
  self.compMode = true
  self.shiftHeld = false
  self.shiftUsed = false
  self.levelSubGraphic = self.subGraphic -- save the GainBias sub-display

  -- Build comp params sub-display
  self.compSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label(args.description or "Band", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.compSubGraphic:addChild(desc)

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

  self.compSubGraphic:addChild(self.threshReadout)
  self.compSubGraphic:addChild(self.ratioReadout)
  self.compSubGraphic:addChild(self.speedReadout)
  self.compSubGraphic:addChild(app.SubButton("thresh", 1))
  self.compSubGraphic:addChild(app.SubButton("ratio", 2))
  self.compSubGraphic:addChild(app.SubButton("speed", 3))

  -- Start in comp mode
  self:setCompMode(true)
end

function CompBandControl:setCompMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.compMode = enabled
  self.compFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.compSubGraphic
  else
    self.subGraphic = self.levelSubGraphic
    self:setFocusedReadout(self.bias)
  end
  self:addSubGraphic(self.subGraphic)
end

function CompBandControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function CompBandControl:onCursorLeave(spot)
  if not self.compMode then
    self:removeSubGraphic(self.subGraphic)
    self.compMode = true
    self.subGraphic = self.compSubGraphic
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function CompBandControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.compFocusedReadout then
    self.shiftSnapshot = self.compFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function CompBandControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.compFocusedReadout and self.shiftSnapshot then
      local cur = self.compFocusedReadout:getValueInUnits()
      if cur ~= self.shiftSnapshot then
        self.shiftHeld = false
        self.shiftSnapshot = nil
        return true
      end
    end
    self:setCompMode(not self.compMode)
  end
  self.shiftHeld = false
  self.shiftSnapshot = nil
  return true
end

function CompBandControl:spotReleased(spot, shifted)
  if not self.compMode then
    self.compFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setCompMode(true)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function CompBandControl:subReleased(i, shifted)
  if shifted then return false end
  if self.compMode then
    local readout = nil
    if i == 1 then readout = self.threshReadout
    elseif i == 2 then readout = self.ratioReadout
    elseif i == 3 then readout = self.speedReadout
    end
    if readout then
      readout:save()
      self.compFocusedReadout = readout
      self:setSubCursorController(readout)
      if not self:hasFocus("encoder") then self:focus() end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function CompBandControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.compMode and self.compFocusedReadout then
    self.compFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function CompBandControl:zeroPressed()
  if self.compMode and self.compFocusedReadout then
    self.compFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function CompBandControl:cancelReleased(shifted)
  if self.compMode and self.compFocusedReadout then
    self.compFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return CompBandControl
