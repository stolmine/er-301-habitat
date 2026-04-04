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

local timeMap = (function()
  local map = app.LinearDialMap(0, 1)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end)()

local levelMap = (function()
  local map = app.LinearDialMap(0, 1)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end)()

local panMap = (function()
  local map = app.LinearDialMap(-1, 1)
  map:setSteps(0.1, 0.01, 0.001, 0.001)
  return map
end)()

local TapListControl = Class {
  type = "TapListControl",
  canEdit = false,
  canMove = true
}
TapListControl:include(Base)

function TapListControl:init(args)
  local description = args.description or "Taps"
  local delay = args.delay or app.logError("%s.init: delay is missing.", self)

  Base.init(self, "taps")
  self:setClassName("TapListControl")

  local width = args.width or ply

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.TapListGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * ply }

  self.delay = delay
  self.currentTap = 0
  self.scrollAccum = 0

  self.levelReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = delay:getParameter("EditTapLevel")
    g:setParameter(param)
    g:setAttributes(app.unitNone, levelMap)
    g:setPrecision(2)
    g:setCenter(col1, center4)
    return g
  end)()

  self.panReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = delay:getParameter("EditTapPan")
    g:setParameter(param)
    g:setAttributes(app.unitNone, panMap)
    g:setPrecision(2)
    g:setCenter(col2, center4)
    return g
  end)()

  self.pitchReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = delay:getParameter("EditTapPitch")
    g:setParameter(param)
    local m = app.LinearDialMap(-24, 24)
    m:setSteps(12, 1, 1, 1)
    m:setRounding(1)
    g:setAttributes(app.unitNone, m)
    g:setPrecision(0)
    g:setCenter(col3, center4)
    return g
  end)()

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
  self.subGraphic:addChild(self.levelReadout)
  self.subGraphic:addChild(self.panReadout)
  self.subGraphic:addChild(self.pitchReadout)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("level", 1))
  self.subGraphic:addChild(app.SubButton("pan", 2))
  self.subGraphic:addChild(app.SubButton("pitch", 3))

  self.pDisplay:follow(delay)
  self.pDisplay:setEditParam(delay:getParameter("EditTapLevel"))
  delay:loadTap(0)
  delay:loadFilter(0)
  self:updateTitle()
end

function TapListControl:updateTitle()
  self.description:setText(string.format("Tap %d", self.currentTap + 1))
end

function TapListControl:switchToTap(newTap)
  local tapCount = self.delay:getTapCount()
  newTap = math.max(0, math.min(tapCount - 1, newTap))
  if newTap == self.currentTap then return end

  self.delay:storeTap(self.currentTap)
  self.delay:storeFilter(self.currentTap)
  self.currentTap = newTap
  self.delay:loadTap(newTap)
  self.delay:loadFilter(newTap)
  self.pDisplay:setSelectedTap(newTap)
  self:updateTitle()
end

function TapListControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function TapListControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function TapListControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function TapListControl:doKeyboardSet(args)
  local Decimal = require "Keyboard.Decimal"
  local keyboard = Decimal {
    message = args.message,
    commitMessage = args.commit,
    initialValue = args.selected:getValueInUnits()
  }
  local task = function(value)
    if value then
      args.selected:save()
      args.selected:setValueInUnits(value)
      self:unfocus()
    end
  end
  keyboard:subscribe("done", task)
  keyboard:subscribe("commit", task)
  keyboard:show()
end

function TapListControl:subReleased(i, shifted)
  if shifted then return false end
  local args = nil
  if i == 1 then
    args = { selected = self.levelReadout, message = "Tap level (0-1).", commit = "Updated level." }
  elseif i == 2 then
    args = { selected = self.panReadout, message = "Tap pan (-1 to 1).", commit = "Updated pan." }
  elseif i == 3 then
    args = { selected = self.pitchReadout, message = "Tap pitch (-2 to +2 octaves).", commit = "Updated pitch." }
  end

  if args then
    if self:hasFocus("encoder") then
      if self.focusedReadout == args.selected then
        self:doKeyboardSet(args)
      else
        self:setFocusedReadout(args.selected)
      end
    else
      self:focus()
      self:setFocusedReadout(args.selected)
    end
  end
  return true
end

function TapListControl:scrollTap(change)
  self.scrollAccum = self.scrollAccum + change
  local steps = math.floor(self.scrollAccum)
  if steps ~= 0 then
    self.scrollAccum = self.scrollAccum - steps
    self:switchToTap(self.currentTap + steps)
  end
end

function TapListControl:encoder(change, shifted)
  if self.focusedReadout and shifted then
    self:scrollTap(change)
    return true
  elseif self.focusedReadout then
    self.focusedReadout:encoder(change, false, self.encoderState == Encoder.Coarse)
    self.delay:storeTap(self.currentTap)
    self.delay:storeFilter(self.currentTap)
    return true
  else
    self:scrollTap(change)
    return true
  end
end

function TapListControl:upReleased(shifted)
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

function TapListControl:onCursorEnter(spot)
  Base.onCursorEnter(self, spot)
  self.pDisplay:setFocused(true)
end

function TapListControl:onCursorLeave(spot)
  self.pDisplay:setFocused(false)
  Base.onCursorLeave(self, spot)
end

return TapListControl
