local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
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

local typeMap = (function()
  local m = app.LinearDialMap(0, 10)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local paramMap = (function()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end)()

local ticksMap = (function()
  local m = app.LinearDialMap(1, 16)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local LaretStepListControl = Class {
  type = "LaretStepListControl",
  canEdit = false,
  canMove = true
}
LaretStepListControl:include(Base)

function LaretStepListControl:init(args)
  local description = args.description or "Steps"
  local op = args.op or app.logError("%s.init: op is missing.", self)

  Base.init(self, "steps")
  self:setClassName("LaretStepListControl")

  local width = args.width or ply

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libspreadsheet.LaretStepListGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * ply }

  self.op = op
  self.currentStep = 0
  self.scrollAccum = 0

  self.typeReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = op:getParameter("EditType")
    g:setParameter(param)
    g:setAttributes(app.unitNone, typeMap)
    g:setPrecision(0)
    g:setCenter(col1, center4)
    -- Kill target/value lag: softSet's 50-step ramp made the displayed
    -- target name disagree with the stored (interpolated) value that
    -- storeStep saw. Discrete selector should never interpolate anyway.
    if g.useHardSet then g:useHardSet() end
    if g.addName then
      g:addName("off"); g:addName("stt"); g:addName("rev"); g:addName("bit")
      g:addName("dec"); g:addName("flt"); g:addName("pch"); g:addName("drv")
      g:addName("shf"); g:addName("dly"); g:addName("cmb")
    end
    return g
  end)()

  self.paramReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = op:getParameter("EditParam")
    g:setParameter(param)
    g:setAttributes(app.unitNone, paramMap)
    g:setPrecision(2)
    g:setCenter(col2, center4)
    if g.useHardSet then g:useHardSet() end
    return g
  end)()

  self.ticksReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = op:getParameter("EditTicks")
    g:setParameter(param)
    g:setAttributes(app.unitNone, ticksMap)
    g:setPrecision(0)
    g:setCenter(col3, center4)
    if g.useHardSet then g:useHardSet() end
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
  self.subGraphic:addChild(self.typeReadout)
  self.subGraphic:addChild(self.paramReadout)
  self.subGraphic:addChild(self.ticksReadout)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("type", 1))
  self.subGraphic:addChild(app.SubButton("param", 2))
  self.subGraphic:addChild(app.SubButton("ticks", 3))

  self.pDisplay:follow(op)
  op:loadStep(0)
  self:updateTitle()
end

function LaretStepListControl:updateTitle()
  self.description:setText(string.format("Step %d", self.currentStep + 1))
end

function LaretStepListControl:switchToStep(newStep)
  local count = self.op:getStepCount()
  newStep = math.max(0, math.min(count - 1, newStep))
  if newStep == self.currentStep then return end

  self.op:storeStep(self.currentStep)
  self.currentStep = newStep
  self.op:loadStep(newStep)
  self.pDisplay:setSelectedStep(newStep)
  self:updateTitle()
end

function LaretStepListControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function LaretStepListControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function LaretStepListControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function LaretStepListControl:doKeyboardSet(args)
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

function LaretStepListControl:subReleased(i, shifted)
  if shifted then return false end
  local args = nil
  if i == 1 then
    args = { selected = self.typeReadout, message = "Effect type (0=off, 1-10).", commit = "Updated type." }
  elseif i == 2 then
    args = { selected = self.paramReadout, message = "Effect param (0-1).", commit = "Updated param." }
  elseif i == 3 then
    args = { selected = self.ticksReadout, message = "Step ticks (1-16).", commit = "Updated ticks." }
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

function LaretStepListControl:scrollStep(change)
  self.scrollAccum = self.scrollAccum + change
  local steps = math.floor(self.scrollAccum)
  if steps ~= 0 then
    self.scrollAccum = self.scrollAccum - steps
    self:switchToStep(self.currentStep + steps)
  end
end

function LaretStepListControl:encoder(change, shifted)
  if self.focusedReadout and shifted then
    self:scrollStep(change)
    return true
  elseif self.focusedReadout then
    self.focusedReadout:encoder(change, false, self.encoderState == Encoder.Fine)
    self.op:storeStep(self.currentStep)
    return true
  else
    self:scrollStep(change)
    return true
  end
end

function LaretStepListControl:upReleased(shifted)
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

function LaretStepListControl:onCursorEnter(spot)
  Base.onCursorEnter(self, spot)
  self.pDisplay:setFocused(true)
end

function LaretStepListControl:onCursorLeave(spot)
  self.pDisplay:setFocused(false)
  Base.onCursorLeave(self, spot)
end

return LaretStepListControl
