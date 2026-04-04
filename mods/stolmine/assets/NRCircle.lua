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

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(2, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local NRCircle = Class {
  type    = "NRCircle",
  canEdit = false,
  canMove = true
}
NRCircle:include(Base)

function NRCircle:init(args)
  local description = args.description or app.logError("%s.init: description is missing.", self)
  local nr = args.nr or app.logError("%s.init: nr is missing.", self)

  Base.init(self, "circle")
  self:setClassName("NRCircle")

  local width = args.width or (2 * ply)

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.NRCircle(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  for i = 1, (width // ply) do
    self:addSpotDescriptor {
      center = (i - 0.5) * ply
    }
  end

  self.prime = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.prime or nr:getParameter("Prime")
    param:enableSerialization()
    g:setParameter(param)
    g:setAttributes(app.unitNone, intMap(0, 31))
    g:setPrecision(0)
    g:setCenter(col1, center4)
    return g
  end)()

  self.mask = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.mask or nr:getParameter("Mask")
    param:enableSerialization()
    g:setParameter(param)
    g:setAttributes(app.unitNone, intMap(0, 3))
    g:setPrecision(0)
    g:setCenter(col2, center4)
    return g
  end)()

  self.factor = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.factor or nr:getParameter("Factor")
    param:enableSerialization()
    g:setParameter(param)
    g:setAttributes(app.unitNone, intMap(0, 16))
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

  local art = (function()
    local draw = app.DrawingInstructions()
    local g = app.Drawing(0, 0, 128, 64)
    g:add(draw)
    return g
  end)()

  self.subGraphic = app.Graphic(0, 0, 128, 64)
  self.subGraphic:addChild(art)
  self.subGraphic:addChild(self.prime)
  self.subGraphic:addChild(self.mask)
  self.subGraphic:addChild(self.factor)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("prime", 1))
  self.subGraphic:addChild(app.SubButton("mask", 2))
  self.subGraphic:addChild(app.SubButton("factor", 3))

  self:follow(nr)
end

function NRCircle:follow(nr)
  self.pDisplay:follow(nr)
  self.nr = nr
end

function NRCircle:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function NRCircle:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function NRCircle:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function NRCircle:doKeyboardSet(args)
  local Decimal = require "Keyboard.Decimal"
  local keyboard = Decimal {
    message       = args.message,
    commitMessage = args.commit,
    initialValue  = args.selected:getValueInUnits(),
    integerOnly   = true
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

function NRCircle:subReleased(i, shifted)
  if shifted then return false end
  local args = nil
  if i == 1 then
    args = { selected = self.prime, message = "Prime pattern (0-31).", commit = "Updated prime." }
  elseif i == 2 then
    args = { selected = self.mask, message = "Mask variant (0-3).", commit = "Updated mask." }
  elseif i == 3 then
    args = { selected = self.factor, message = "Factor (0-16).", commit = "Updated factor." }
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

function NRCircle:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
  end
  return true
end

return NRCircle
