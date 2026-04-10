local app = app
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

local CompSidechainControl = Class {
  type = "CompSidechainControl",
  canEdit = false,
  canMove = true
}
CompSidechainControl:include(Base)

function CompSidechainControl:init(args)
  Base.init(self, args.button or "side")
  self:setClassName("CompSidechainControl")

  self.branch = args.branch
  self.compressor = args.compressor

  -- Main graphic: fader for input gain
  self.fader = (function()
    local f = app.Fader(0, 0, ply, 64)
    f:setParameter(args.inputGainParam)
    f:setLabel(args.button or "side")
    f:setAttributes(args.units or app.unitNone, args.map)
    f:setPrecision(2)
    return f
  end)()

  self:setMainCursorController(self.fader)
  self:setControlGraphic(self.fader)
  self:addSpotDescriptor { center = 0.5 * ply }

  -- Sub-display: scope, enable toggle, gain readout
  self.subGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label(args.description or "Sidechain", 10)
  desc:fitToText(3)
  desc:setSize(ply * 2, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(0.5 * (col2 + col3), center1 + 1)
  self.subGraphic:addChild(desc)

  -- MiniScope watching sidechain branch
  self.scope = app.MiniScope(col1 - 20, app.GRID5_LINE4, 40, 45)
  self.scope:setBorder(1)
  self.scope:setCornerRadius(3, 3, 3, 3)
  self.subGraphic:addChild(self.scope)

  -- Enable indicator
  self.enableIndicator = app.BinaryIndicator(0, 24, ply, 32)
  self.enableIndicator:setCenter(col2, center3)
  self.subGraphic:addChild(self.enableIndicator)

  -- Gain readout
  self.gainReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.inputGainParam)
    g:setAttributes(args.units or app.unitNone, args.map)
    g:setPrecision(2)
    g:setCenter(col3, center4)
    return g
  end)()
  self.subGraphic:addChild(self.gainReadout)

  -- Drawing: arrows connecting scope -> enable -> title
  local drawing = app.Drawing(0, 0, 128, 64)
  local instructions = app.DrawingInstructions()
  instructions:hline(col1 + 20, col2 - 10, center3)
  instructions:circle(col2, center3, 8)
  instructions:vline(col2, center3 + 8, app.GRID5_LINE1 - 2)
  instructions:triangle(col2, app.GRID5_LINE1 - 2, 90, 3)
  drawing:add(instructions)
  self.subGraphic:addChild(drawing)

  self.subGraphic:addChild(app.SubButton("side", 1))
  self.subGraphic:addChild(app.SubButton("enable", 2))
  self.subGraphic:addChild(app.SubButton("gain", 3))

  self:updateState()
  self.branch:subscribe("contentChanged", self)
end

function CompSidechainControl:onRemove()
  self.branch:unsubscribe("contentChanged", self)
  Base.onRemove(self)
end

function CompSidechainControl:updateState()
  if self.compressor:isSidechainEnabled() then
    self.enableIndicator:on()
  else
    self.enableIndicator:off()
  end
end

function CompSidechainControl:contentChanged(chain)
  if chain == self.branch then
    self.scope:watchOutlet(chain:getMonitoringOutput(1))
  end
end

function CompSidechainControl:onCursorEnter()
  self:updateState()
  return Base.onCursorEnter(self)
end

function CompSidechainControl:subReleased(i, shifted)
  if shifted then return false end
  if i == 1 then
    self:unfocus()
    self.branch:show()
  elseif i == 2 then
    self.compressor:toggleSidechainEnabled()
    self:updateState()
  elseif i == 3 then
    self.gainReadout:save()
    self:setSubCursorController(self.gainReadout)
    if not self:hasFocus("encoder") then self:focus() end
  end
  return true
end

function CompSidechainControl:encoder(change, shifted)
  self.fader:encoder(change, shifted, self.encoderState == Encoder.Fine)
  return true
end

function CompSidechainControl:zeroPressed()
  self.fader:zero()
  return true
end

function CompSidechainControl:cancelReleased(shifted)
  if not shifted then self.fader:restore() end
  return true
end

function CompSidechainControl:onFocused()
  self.fader:save()
end

return CompSidechainControl
