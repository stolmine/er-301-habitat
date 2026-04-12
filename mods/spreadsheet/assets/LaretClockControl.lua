local app = app
local Class = require "Base.Class"
local Base = require "Unit.ViewControl.EncoderControl"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local line1 = app.GRID5_LINE1
local line4 = app.GRID5_LINE4
local center1 = app.GRID5_CENTER1
local center3 = app.GRID5_CENTER3
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local instructions = app.DrawingInstructions()
instructions:box(col2 - 13, center3 - 8, 26, 16)
instructions:startPolyline(col2 - 8, center3 - 4, 0)
instructions:vertex(col2, center3 - 4)
instructions:vertex(col2, center3 + 4)
instructions:endPolyline(col2 + 8, center3 + 4)
instructions:color(app.GRAY3)
instructions:hline(col2 - 9, col2 + 9, center3)
instructions:color(app.WHITE)
instructions:circle(col3, center3, 8)
instructions:hline(col1 + 20, col2 - 13, center3)
instructions:triangle(col2 - 16, center3, 0, 3)
instructions:hline(col2 + 13, col3 - 8, center3)
instructions:triangle(col3 - 11, center3, 0, 3)
instructions:vline(col3, center3 + 8, line1 - 2)
instructions:triangle(col3, line1 - 2, 90, 3)
instructions:vline(col3, line4, center3 - 8)
instructions:triangle(col3, center3 - 11, 90, 3)

local divMap = (function()
  local m = app.LinearDialMap(1, 16)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local LaretClockControl = Class {
  type = "LaretClockControl",
  canEdit = false,
  canMove = true
}
LaretClockControl:include(Base)

function LaretClockControl:init(args)
  local button = args.button or "clock"
  local description = args.description or "Clock"
  local branch = args.branch or app.logError("%s.init: branch is missing.", self)
  local comparator = args.comparator or app.logError("%s.init: comparator is missing.", self)
  local resetComparator = args.resetComparator or app.logError("%s.init: resetComparator is missing.", self)
  local divParam = args.divParam or app.logError("%s.init: divParam is missing.", self)

  Base.init(self, button)
  self:setClassName("LaretClockControl")

  self.branch = branch
  self.comparator = comparator
  self.resetComparator = resetComparator

  local graphic = app.ComparatorView(0, 0, ply, 64, comparator)
  graphic:setLabel(button)
  self.comparatorView = graphic
  self:setMainCursorController(graphic)
  self:setControlGraphic(graphic)
  self:addSpotDescriptor { center = 0.5 * ply }

  -- Sub display
  self.subGraphic = app.Graphic(0, 0, 128, 64)

  graphic = app.Drawing(0, 0, 128, 64)
  graphic:add(instructions)
  self.subGraphic:addChild(graphic)

  graphic = app.Label("or", 10)
  graphic:fitToText(0)
  graphic:setCenter(col3, center3 + 1)
  self.subGraphic:addChild(graphic)

  self.scope = app.MiniScope(col1 - 20, line4, 40, 45)
  self.scope:setBorder(1)
  self.scope:setCornerRadius(3, 3, 3, 3)
  self.subGraphic:addChild(self.scope)

  -- Division readout
  self.divReadout = app.Readout(0, 0, ply, 10)
  self.divReadout:setParameter(divParam)
  self.divReadout:setAttributes(app.unitNone, divMap)
  self.divReadout:setPrecision(0)
  self.divReadout:setCenter(col2, center4)
  self.subGraphic:addChild(self.divReadout)

  graphic = app.Label(description, 10)
  graphic:fitToText(3)
  graphic:setSize(ply * 2, graphic.mHeight)
  graphic:setBorder(1)
  graphic:setCornerRadius(3, 0, 0, 3)
  graphic:setCenter(0.5 * (col2 + col3), center1 + 1)
  self.subGraphic:addChild(graphic)

  graphic = app.SubButton("input", 1)
  self.subGraphic:addChild(graphic)
  self.modButton = graphic

  graphic = app.SubButton("div", 2)
  self.subGraphic:addChild(graphic)

  graphic = app.SubButton("reset", 3)
  self.subGraphic:addChild(graphic)

  self.focusedReadout = nil

  branch:subscribe("contentChanged", self)
end

function LaretClockControl:onRemove()
  self.branch:unsubscribe("contentChanged", self)
  Base.onRemove(self)
end

function LaretClockControl:contentChanged(chain)
  if chain == self.branch then
    local outlet = chain:getMonitoringOutput(1)
    self.scope:watchOutlet(outlet)
    self.modButton:setText(chain:mnemonic())
  end
end

function LaretClockControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function LaretClockControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function LaretClockControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function LaretClockControl:spotReleased(spot, shifted)
  if Base.spotReleased(self, spot, shifted) then
    self:setFocusedReadout(nil)
    return true
  end
  return false
end

function LaretClockControl:enterReleased()
  if Base.enterReleased(self) then
    self:setFocusedReadout(nil)
    return true
  end
  return false
end

function LaretClockControl:subPressed(i, shifted)
  if shifted then return false end
  if i == 3 then
    self.resetComparator:simulateRisingEdge()
  end
  return true
end

function LaretClockControl:subReleased(i, shifted)
  if shifted then return false end
  if i == 1 then
    if self.branch then
      self:unfocus()
      self.branch:show()
    end
  elseif i == 2 then
    if self:hasFocus("encoder") then
      if self.focusedReadout == self.divReadout then
        local Decimal = require "Keyboard.Decimal"
        local kb = Decimal {
          message = "Clock division.",
          commitMessage = "Clock division updated.",
          initialValue = self.divReadout:getValueInUnits()
        }
        local task = function(value)
          if value then
            self.divReadout:save()
            self.divReadout:setValueInUnits(value)
            self:unfocus()
          end
        end
        kb:subscribe("done", task)
        kb:subscribe("commit", task)
        kb:show()
      else
        self:setFocusedReadout(self.divReadout)
      end
    else
      self:focus()
      self:setFocusedReadout(self.divReadout)
    end
  elseif i == 3 then
    self.resetComparator:simulateFallingEdge()
  end
  return true
end

function LaretClockControl:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
  end
  return true
end

function LaretClockControl:upReleased(shifted)
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

return LaretClockControl
