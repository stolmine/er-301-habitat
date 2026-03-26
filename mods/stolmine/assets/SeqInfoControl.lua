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

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(4, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local seqLenMap = intMap(1, 64)
local loopLenMap = intMap(0, 64)

local SeqInfoControl = Class {
  type = "SeqInfoControl",
  canEdit = false,
  canMove = true
}
SeqInfoControl:include(Base)

function SeqInfoControl:init(args)
  local seq = args.seq or app.logError("%s.init: seq is missing.", self)

  Base.init(self, "info")
  self:setClassName("SeqInfoControl")

  local width = args.width or ply

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.SeqInfoGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * ply }

  -- Sub-display readouts for sequence length and loop length
  self.seqLenReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.seqLength
    if param then
      param:enableSerialization()
      g:setParameter(param)
    end
    g:setAttributes(app.unitNone, seqLenMap)
    g:setPrecision(0)
    g:setCenter(col1, center4)
    return g
  end)()

  self.loopLenReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.loopLength
    if param then
      param:enableSerialization()
      g:setParameter(param)
    end
    g:setAttributes(app.unitNone, loopLenMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    return g
  end)()

  self.description = (function()
    local g = app.Label("Sequence", 10)
    g:fitToText(3)
    g:setSize(ply * 3, g.mHeight)
    g:setBorder(1)
    g:setCornerRadius(3, 0, 0, 3)
    g:setCenter(col1 + ply / 2, center1 + 1)
    return g
  end)()

  self.subGraphic = app.Graphic(0, 0, 128, 64)
  self.subGraphic:addChild(self.seqLenReadout)
  self.subGraphic:addChild(self.loopLenReadout)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("length", 1))
  self.subGraphic:addChild(app.SubButton("loop", 2))

  self.pDisplay:follow(seq)
end

function SeqInfoControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function SeqInfoControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function SeqInfoControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function SeqInfoControl:subReleased(i, shifted)
  if shifted then return false end
  local readout = i == 1 and self.seqLenReadout or i == 2 and self.loopLenReadout or nil
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

function SeqInfoControl:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Coarse)
  end
  return true
end

return SeqInfoControl
