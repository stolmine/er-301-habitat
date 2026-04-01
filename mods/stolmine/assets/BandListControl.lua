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

local freqMap = (function()
  local map = app.LinearDialMap(20, 16000)
  map:setSteps(100, 10, 1, 1)
  return map
end)()

local gainMap = (function()
  local map = app.LinearDialMap(0, 4)
  map:setSteps(0.5, 0.1, 0.01, 0.001)
  return map
end)()

local typeMap = (function()
  local m = app.LinearDialMap(0, 2)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local typeNames = { [0] = "peak", "bpf", "res" }

local BandListControl = Class {
  type = "BandListControl",
  canEdit = false,
  canMove = true
}
BandListControl:include(Base)

function BandListControl:init(args)
  local description = args.description or "Bands"
  local fb = args.filterbank or app.logError("%s.init: filterbank is missing.", self)

  Base.init(self, "bands")
  self:setClassName("BandListControl")

  local width = args.width or ply

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.BandListGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * ply }

  self.fb = fb
  self.currentBand = 0
  self.scrollAccum = 0

  self.freqReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = fb:getParameter("EditFreq")
    g:setParameter(param)
    g:setAttributes(app.unitHertz, freqMap)
    g:setPrecision(0)
    g:setCenter(col1, center4)
    return g
  end)()

  self.gainReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = fb:getParameter("EditGain")
    g:setParameter(param)
    g:setAttributes(app.unitNone, gainMap)
    g:setPrecision(2)
    g:setCenter(col2, center4)
    return g
  end)()

  self.typeReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = fb:getParameter("EditType")
    g:setParameter(param)
    g:setAttributes(app.unitNone, typeMap)
    g:setPrecision(0)
    g:setCenter(col3, center4)
    return g
  end)()

  self.typeLabel = app.Label("peak", 10)
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
  self.subGraphic:addChild(self.freqReadout)
  self.subGraphic:addChild(self.gainReadout)
  self.subGraphic:addChild(self.typeReadout)
  self.subGraphic:addChild(self.typeLabel)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("freq", 1))
  self.subGraphic:addChild(app.SubButton("gain", 2))
  self.subGraphic:addChild(app.SubButton("type", 3))

  self.pDisplay:follow(fb)
  self.pDisplay:setEditParam(fb:getParameter("EditFreq"))
  fb:loadBand(0)
  self:updateTitle()
end

function BandListControl:updateTitle()
  self.description:setText(string.format("Band %d", self.currentBand + 1))
end

function BandListControl:updateTypeLabel()
  local val = math.floor(self.typeReadout:getValueInUnits() + 0.5)
  local name = typeNames[val]
  if name then
    self.typeLabel:setText(name)
  end
end

function BandListControl:switchToBand(newBand)
  local bandCount = self.fb:getBandCount()
  newBand = math.max(0, math.min(bandCount - 1, newBand))
  if newBand == self.currentBand then return end

  self.fb:storeBand(self.currentBand)
  self.currentBand = newBand
  self.fb:loadBand(newBand)
  self.pDisplay:setSelectedBand(newBand)
  self:updateTitle()
  self:updateTypeLabel()
end

function BandListControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function BandListControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function BandListControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function BandListControl:doKeyboardSet(args)
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

function BandListControl:subReleased(i, shifted)
  if shifted then return false end
  local args = nil
  if i == 1 then
    args = { selected = self.freqReadout, message = "Band frequency (20-16000 Hz).", commit = "Updated freq." }
  elseif i == 2 then
    args = { selected = self.gainReadout, message = "Band gain (0-4).", commit = "Updated gain." }
  elseif i == 3 then
    args = { selected = self.typeReadout, message = "Filter type (0=peak, 1=bpf, 2=allpass).", commit = "Updated type." }
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

function BandListControl:scrollBand(change)
  self.scrollAccum = self.scrollAccum + change
  local steps = math.floor(self.scrollAccum)
  if steps ~= 0 then
    self.scrollAccum = self.scrollAccum - steps
    self:switchToBand(self.currentBand + steps)
  end
end

function BandListControl:encoder(change, shifted)
  if self.focusedReadout and shifted then
    self:scrollBand(change)
    return true
  elseif self.focusedReadout then
    self.focusedReadout:encoder(change, false, self.encoderState == Encoder.Coarse)
    self.fb:storeBand(self.currentBand)
    if self.focusedReadout == self.typeReadout then
      self:updateTypeLabel()
    end
    return true
  else
    self:scrollBand(change)
    return true
  end
end

function BandListControl:upReleased(shifted)
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

return BandListControl
