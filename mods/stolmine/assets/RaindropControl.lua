local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Base = require "Unit.ViewControl.EncoderControl"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local stackNames = { [0] = "1", "2", "4", "8", "16" }

local RaindropControl = Class {
  type = "RaindropControl",
  canEdit = false,
  canMove = true
}
RaindropControl:include(Base)

function RaindropControl:init(args)
  local delay = args.delay or app.logError("%s.init: delay is missing.", self)

  Base.init(self, "overview")
  self:setClassName("RaindropControl")

  local width = args.width or ply

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.RaindropGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * width }

  self.pDisplay:follow(delay)

  -- Sub-display: always visible param readouts
  local subGraphic = app.Graphic(0, 0, 128, 64)

  local grainMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  local tapCountMap = (function()
    local m = app.LinearDialMap(1, 16)
    m:setSteps(4, 1, 0.25, 0.25)
    m:setRounding(1)
    return m
  end)()

  local stackMap = (function()
    local m = app.LinearDialMap(0, 4)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  local function makeReadout(param, map, precision, units, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(units, map)
    g:setPrecision(precision)
    g:setCenter(x, center4)
    return g
  end

  self.grainReadout = makeReadout(args.grainSize, grainMap, 2, app.unitNone, col1)
  self.tapCountReadout = makeReadout(args.tapCount, tapCountMap, 0, app.unitNone, col2)

  -- Stack: hidden readout for encoder control, visible label for display
  self.stackReadout = makeReadout(args.stack, stackMap, 0, app.unitNone, -ply)

  self.stackLabel = app.Label("1", 10)
  self.stackLabel:fitToText(0)
  self.stackLabel:setCenter(col3, center4)

  local desc = app.Label("Grain / Taps / Stack", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  subGraphic:addChild(self.grainReadout)
  subGraphic:addChild(self.tapCountReadout)
  subGraphic:addChild(self.stackReadout)
  subGraphic:addChild(self.stackLabel)
  subGraphic:addChild(desc)
  subGraphic:addChild(app.SubButton("grain", 1))
  subGraphic:addChild(app.SubButton("taps", 2))
  subGraphic:addChild(app.SubButton("stack", 3))

  self.subGraphic = subGraphic
  self.focusedReadout = nil
  self:updateStackLabel()
end

function RaindropControl:updateStackLabel()
  local val = math.floor(self.stackReadout:getValueInUnits() + 0.5)
  local name = stackNames[val] or tostring(val)
  self.stackLabel:setText(name)
end

function RaindropControl:setSelectedTap(tap)
  self.pDisplay:setSelectedTap(tap)
end

function RaindropControl:subReleased(i, shifted)
  if shifted then return false end
  local readout = i == 1 and self.grainReadout
      or i == 2 and self.tapCountReadout
      or i == 3 and self.stackReadout or nil
  if readout then
    readout:save()
    self.focusedReadout = readout
    self:setSubCursorController(readout)
    if not self:hasFocus("encoder") then self:focus() end
    return true
  end
  return Base.subReleased(self, i, shifted)
end

function RaindropControl:spotReleased(spot, shifted)
  self.focusedReadout = nil
  self:setSubCursorController(nil)
  return Base.spotReleased(self, spot, shifted)
end

function RaindropControl:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Coarse)
    if self.focusedReadout == self.stackReadout then
      self:updateStackLabel()
    end
    return true
  end
  return true
end

function RaindropControl:zeroPressed()
  if self.focusedReadout then
    self.focusedReadout:zero()
    if self.focusedReadout == self.stackReadout then
      self:updateStackLabel()
    end
    return true
  end
  return true
end

function RaindropControl:cancelReleased(shifted)
  if self.focusedReadout then
    self.focusedReadout:restore()
    if self.focusedReadout == self.stackReadout then
      self:updateStackLabel()
    end
    return true
  end
  return true
end

return RaindropControl
