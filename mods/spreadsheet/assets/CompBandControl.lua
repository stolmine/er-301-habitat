local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Base = require "Unit.ViewControl.EncoderControl"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local CompBandControl = Class {
  type = "CompBandControl",
  canEdit = false,
  canMove = true
}
CompBandControl:include(Base)

function CompBandControl:init(args)
  local button = args.button or "band"
  Base.init(self, button)
  self:setClassName("CompBandControl")

  local compressor = args.compressor
  local bandIndex = args.bandIndex or 0

  -- Spectrum graphic replacing fader
  local spectrum = libspreadsheet.CompressorSpectrumGraphic(0, 0, ply, 64)
  spectrum:follow(compressor)
  spectrum:setBandIndex(bandIndex)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(spectrum)
  self:setMainCursorController(spectrum)
  self:setControlGraphic(container)
  self:addSpotDescriptor { center = 0.5 * ply }

  -- Sub-display: threshold / ratio / speed
  self.subGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label(args.description or "Band", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.subGraphic:addChild(desc)

  local function makeReadout(param, map, precision, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(app.unitNone, map)
    g:setPrecision(precision)
    g:setCenter(x, center4)
    return g
  end

  local threshMap = (function()
    local m = app.LinearDialMap(0.01, 1)
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

  self.subGraphic:addChild(self.threshReadout)
  self.subGraphic:addChild(self.ratioReadout)
  self.subGraphic:addChild(self.speedReadout)

  self.subGraphic:addChild(app.SubButton("thresh", 1))
  self.subGraphic:addChild(app.SubButton("ratio", 2))
  self.subGraphic:addChild(app.SubButton("speed", 3))

  self.focusedReadout = nil
end

function CompBandControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function CompBandControl:subReleased(i, shifted)
  if shifted then return false end
  local readout = nil
  if i == 1 then readout = self.threshReadout
  elseif i == 2 then readout = self.ratioReadout
  elseif i == 3 then readout = self.speedReadout
  end
  if readout then
    if self:hasFocus("encoder") then
      self:setFocusedReadout(readout)
    else
      self:focus()
      self:setFocusedReadout(readout)
    end
  end
  return true
end

function CompBandControl:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return false
end

function CompBandControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function CompBandControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function CompBandControl:upReleased(shifted)
  if self.focusedReadout then
    self.focusedReadout = nil
    self:setSubCursorController(nil)
    return true
  elseif self:hasFocus("encoder") then
    self:unfocus()
    return true
  end
  return false
end

return CompBandControl
