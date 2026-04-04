local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Base = require "Unit.ViewControl.EncoderControl"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center3 = app.GRID5_CENTER3
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(4, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local bandCountMap = intMap(2, 16)
local vOctMap = floatMap(-2, 2)
local slewMap = floatMap(0, 5)

local FilterResponseControl = Class {
  type = "FilterResponseControl",
  canEdit = false,
  canMove = true
}
FilterResponseControl:include(Base)

function FilterResponseControl:init(args)
  local fb = args.filterbank or app.logError("%s.init: filterbank is missing.", self)

  Base.init(self, "overview")
  self:setClassName("FilterResponseControl")

  local width = args.width or ply

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.FilterResponseGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * width }

  self.bandCountReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.bandCount
    if param then
      param:enableSerialization()
      g:setParameter(param)
    end
    g:setAttributes(app.unitNone, bandCountMap)
    g:setPrecision(0)
    g:setCenter(col1, center4)
    return g
  end)()

  self.vOctReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.vOctOffset
    if param then
      param:enableSerialization()
      g:setParameter(param)
    end
    g:setAttributes(app.unitNone, vOctMap)
    g:setPrecision(2)
    g:setCenter(col2, center4)
    return g
  end)()

  self.slewReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.slew
    if param then
      param:enableSerialization()
      g:setParameter(param)
    end
    g:setAttributes(app.unitNone, slewMap)
    g:setPrecision(2)
    g:setCenter(col3, center4)
    return g
  end)()

  self.description = (function()
    local g = app.Label("Response", 10)
    g:fitToText(3)
    g:setSize(ply * 3, g.mHeight)
    g:setBorder(1)
    g:setCornerRadius(3, 0, 0, 3)
    g:setCenter(col2, center1 + 1)
    return g
  end)()

  self.subGraphic = app.Graphic(0, 0, 128, 64)
  self.subGraphic:addChild(self.bandCountReadout)
  self.subGraphic:addChild(self.vOctReadout)
  self.subGraphic:addChild(self.slewReadout)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("bands", 1))
  self.subGraphic:addChild(app.SubButton("V/Oct", 2))
  self.subGraphic:addChild(app.SubButton("slew", 3))

  self.pDisplay:follow(fb)
end

function FilterResponseControl:setSelectedBand(band)
  self.pDisplay:setSelectedBand(band)
end

function FilterResponseControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function FilterResponseControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function FilterResponseControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function FilterResponseControl:subReleased(i, shifted)
  if shifted then return false end
  local readout = i == 1 and self.bandCountReadout
      or i == 2 and self.vOctReadout
      or i == 3 and self.slewReadout or nil
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

function FilterResponseControl:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
  end
  return true
end

return FilterResponseControl
