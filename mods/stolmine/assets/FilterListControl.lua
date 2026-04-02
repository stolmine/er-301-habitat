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

local cutoffMap = (function()
  local map = app.LinearDialMap(20, 10000)
  map:setSteps(1000, 100, 10, 1)
  return map
end)()

local qMap = (function()
  local map = app.LinearDialMap(0, 1)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end)()

local typeMap = (function()
  local m = app.LinearDialMap(0, 4)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local typeNames = { [0] = "off", "lp", "bp", "hp", "ntch" }

local FilterListControl = Class {
  type = "FilterListControl",
  canEdit = false,
  canMove = true
}
FilterListControl:include(Base)

function FilterListControl:init(args)
  local description = args.description or "Filters"
  local delay = args.delay or app.logError("%s.init: delay is missing.", self)

  Base.init(self, "filters")
  self:setClassName("FilterListControl")

  local width = args.width or ply

  -- Reuse TapListGraphic for the list display (shows tap index + level)
  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.TapListGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * ply }

  self.delay = delay
  self.currentTap = 0
  self.scrollAccum = 0

  self.cutoffReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = delay:getParameter("EditFilterCutoff")
    g:setParameter(param)
    g:setAttributes(app.unitHertz, cutoffMap)
    g:setPrecision(2)
    g:setCenter(col1, center4)
    return g
  end)()

  self.qReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = delay:getParameter("EditFilterQ")
    g:setParameter(param)
    g:setAttributes(app.unitNone, qMap)
    g:setPrecision(2)
    g:setCenter(col2, center4)
    return g
  end)()

  self.typeReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = delay:getParameter("EditFilterType")
    g:setParameter(param)
    g:setAttributes(app.unitNone, typeMap)
    g:setPrecision(0)
    g:setCenter(col3, center4)
    return g
  end)()

  self.typeLabel = app.Label("lp", 10)
  self.typeLabel:fitToText(0)
  self.typeLabel:setCenter(col3, center3 + 1)

  self.description = (function()
    local g = app.Label(description, 10)
    g:fitToText(3)
    g:setSize(ply * 3, g.mHeight)
    g:setBorder(1)
    g:setCornerRadius(3, 0, 0, 3)
    g:setCenter(col2, center1 + 1)
    return g
  end)()

  self.subGraphic = app.Graphic(0, 0, 128, 64)
  self.subGraphic:addChild(self.cutoffReadout)
  self.subGraphic:addChild(self.qReadout)
  self.subGraphic:addChild(self.typeReadout)
  self.subGraphic:addChild(self.typeLabel)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("freq", 1))
  self.subGraphic:addChild(app.SubButton("Q", 2))
  self.subGraphic:addChild(app.SubButton("type", 3))

  self.pDisplay:follow(delay)
  self.pDisplay:setEditParam(delay:getParameter("EditTapLevel"))
  delay:loadFilter(0)
  self:updateTitle()
  self:updateTypeLabel()
end

function FilterListControl:updateTitle()
  self.description:setText(string.format("Filter %d", self.currentTap + 1))
end

function FilterListControl:updateTypeLabel()
  local val = math.floor(self.typeReadout:getValueInUnits() + 0.5)
  local name = typeNames[val]
  if name then
    self.typeLabel:setText(name)
  end
end

function FilterListControl:switchToTap(newTap)
  local tapCount = self.delay:getTapCount()
  newTap = math.max(0, math.min(tapCount - 1, newTap))
  if newTap == self.currentTap then return end

  self.delay:storeFilter(self.currentTap)
  self.currentTap = newTap
  self.delay:loadFilter(newTap)
  self.pDisplay:setSelectedTap(newTap)
  self:updateTitle()
  self:updateTypeLabel()
end

function FilterListControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function FilterListControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function FilterListControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function FilterListControl:subReleased(i, shifted)
  if shifted then return false end
  local readout = i == 1 and self.cutoffReadout
      or i == 2 and self.qReadout
      or i == 3 and self.typeReadout or nil
  if readout then
    if self:hasFocus("encoder") then
      if self.focusedReadout == readout then
        -- keyboard entry
      else
        self:setFocusedReadout(readout)
      end
    else
      self:focus()
      self:setFocusedReadout(readout)
    end
  end
  return true
end

function FilterListControl:scrollTap(change)
  self.scrollAccum = self.scrollAccum + change
  local steps = math.floor(self.scrollAccum)
  if steps ~= 0 then
    self.scrollAccum = self.scrollAccum - steps
    self:switchToTap(self.currentTap + steps)
  end
end

function FilterListControl:encoder(change, shifted)
  if self.focusedReadout and shifted then
    self:scrollTap(change)
    return true
  elseif self.focusedReadout then
    self.focusedReadout:encoder(change, false, self.encoderState == Encoder.Coarse)
    self.delay:storeFilter(self.currentTap)
    if self.focusedReadout == self.typeReadout then
      self:updateTypeLabel()
    end
    return true
  else
    self:scrollTap(change)
    return true
  end
end

function FilterListControl:upReleased(shifted)
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

return FilterListControl
