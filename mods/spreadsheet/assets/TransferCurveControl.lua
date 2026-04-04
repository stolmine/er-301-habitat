local app = app
local libstolmine = require "spreadsheet.libspreadsheet"
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

local deviationMap = floatMap(0, 1)
local segCountMap = intMap(4, 32)

local scopeMap = (function()
  local m = app.LinearDialMap(0, 3)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local scopeNames = { [0] = "ofst", "crv", "wgt", "all" }

local TransferCurveControl = Class {
  type = "TransferCurveControl",
  canEdit = false,
  canMove = true
}
TransferCurveControl:include(Base)

function TransferCurveControl:init(args)
  local etcher = args.etcher or app.logError("%s.init: etcher is missing.", self)

  Base.init(self, "curve")
  self:setClassName("TransferCurveControl")

  local width = args.width or (2 * ply)

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.TransferCurveGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * width }

  -- Sub-display readouts
  self.deviationReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.deviation
    if param then
      param:enableSerialization()
      g:setParameter(param)
    end
    g:setAttributes(app.unitNone, deviationMap)
    g:setPrecision(2)
    g:setCenter(col1, center4)
    return g
  end)()

  self.scopeReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.deviationScope
    if param then
      g:setParameter(param)
    end
    g:setAttributes(app.unitNone, scopeMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    return g
  end)()

  self.segCountReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.segCount
    if param then
      param:enableSerialization()
      g:setParameter(param)
    end
    g:setAttributes(app.unitNone, segCountMap)
    g:setPrecision(0)
    g:setCenter(col3, center4)
    return g
  end)()

  self.scopeLabel = app.Label("ofst", 10)
  self.scopeLabel:fitToText(0)
  self.scopeLabel:setCenter(col2, center3 + 1)

  self.description = (function()
    local g = app.Label("Curve", 10)
    g:fitToText(3)
    g:setSize(ply * 3, g.mHeight)
    g:setBorder(1)
    g:setCornerRadius(3, 0, 0, 3)
    g:setCenter(col2, center1 + 1)
    return g
  end)()

  self.subGraphic = app.Graphic(0, 0, 128, 64)
  self.subGraphic:addChild(self.deviationReadout)
  self.subGraphic:addChild(self.scopeReadout)
  self.subGraphic:addChild(self.segCountReadout)
  self.subGraphic:addChild(self.scopeLabel)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("dev", 1))
  self.subGraphic:addChild(app.SubButton("scope", 2))
  self.subGraphic:addChild(app.SubButton("segs", 3))

  self.pDisplay:follow(etcher)
end

function TransferCurveControl:setSelectedSegment(seg)
  self.pDisplay:setSelectedSegment(seg)
end

function TransferCurveControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function TransferCurveControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function TransferCurveControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function TransferCurveControl:subReleased(i, shifted)
  if shifted then return false end
  local readout = i == 1 and self.deviationReadout
      or i == 2 and self.scopeReadout
      or i == 3 and self.segCountReadout or nil
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

function TransferCurveControl:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    if self.focusedReadout == self.scopeReadout then
      local val = math.floor(self.scopeReadout:getValueInUnits() + 0.5)
      local name = scopeNames[val]
      if name then
        self.scopeLabel:setText(name)
      end
    end
  end
  return true
end

return TransferCurveControl
