local app = app
local libmi = require "mi.libmi"
local Class = require "Base.Class"
local Base = require "Unit.ViewControl.EncoderControl"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local GridsCircle = Class {
  type    = "GridsCircle",
  canEdit = false,
  canMove = true
}
GridsCircle:include(Base)

function GridsCircle:init(args)
  local description = args.description or app.logError("%s.init: description is missing.", self)
  local grids = args.grids or app.logError("%s.init: grids is missing.", self)

  Base.init(self, "circle")
  self:setClassName("GridsCircle")

  local width = args.width or (2 * ply)

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libmi.GridsCircle(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  for i = 1, (width // ply) do
    self:addSpotDescriptor {
      center = (i - 0.5) * ply
    }
  end

  self.mapx = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.mapx
    param:enableSerialization()
    g:setParameter(param)
    g:setAttributes(app.unitNone, Encoder.getMap("[0,1]"))
    g:setPrecision(2)
    g:setCenter(col1, center4)
    return g
  end)()

  self.mapy = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.mapy
    param:enableSerialization()
    g:setParameter(param)
    g:setAttributes(app.unitNone, Encoder.getMap("[0,1]"))
    g:setPrecision(2)
    g:setCenter(col2, center4)
    return g
  end)()

  self.density = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.density
    param:enableSerialization()
    g:setParameter(param)
    g:setAttributes(app.unitNone, Encoder.getMap("[0,1]"))
    g:setPrecision(2)
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
  self.subGraphic:addChild(self.mapx)
  self.subGraphic:addChild(self.mapy)
  self.subGraphic:addChild(self.density)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("x", 1))
  self.subGraphic:addChild(app.SubButton("y", 2))
  self.subGraphic:addChild(app.SubButton("fill", 3))

  self:follow(grids)
end

function GridsCircle:follow(grids)
  self.pDisplay:follow(grids)
  self.grids = grids
end

function GridsCircle:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function GridsCircle:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function GridsCircle:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function GridsCircle:doKeyboardSet(args)
  local Decimal = require "Keyboard.Decimal"
  local keyboard = Decimal {
    message       = args.message,
    commitMessage = args.commit,
    initialValue  = args.selected:getValueInUnits()
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

function GridsCircle:subReleased(i, shifted)
  if shifted then return false end
  local args = nil
  if i == 1 then
    args = { selected = self.mapx, message = "Map X (0-1).", commit = "Updated X." }
  elseif i == 2 then
    args = { selected = self.mapy, message = "Map Y (0-1).", commit = "Updated Y." }
  elseif i == 3 then
    args = { selected = self.density, message = "Density (0-1).", commit = "Updated density." }
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

function GridsCircle:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Coarse)
  end
  return true
end

return GridsCircle
