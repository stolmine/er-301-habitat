local app = app
local libstolmine = require "spreadsheet.libspreadsheet"
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

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end

local offsetMap = floatMap(-1, 1)
local weightMap = floatMap(0.1, 4.0)

local curveMap = (function()
  local m = app.LinearDialMap(0, 2)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local curveNames = { [0] = "step", "lin", "cubic" }

local SegmentListControl = Class {
  type = "SegmentListControl",
  canEdit = false,
  canMove = true
}
SegmentListControl:include(Base)

function SegmentListControl:init(args)
  local description = args.description or "Segments"
  local etcher = args.etcher or app.logError("%s.init: etcher is missing.", self)

  Base.init(self, "segments")
  self:setClassName("SegmentListControl")

  local width = args.width or ply

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.SegmentListGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * ply }

  self.etcher = etcher
  self.currentSegment = 0
  self.scrollAccum = 0

  -- Sub-display readouts bound to edit buffer params
  self.offsetReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = etcher:getParameter("EditOffset")
    g:setParameter(param)
    g:setAttributes(app.unitNone, offsetMap)
    g:setPrecision(2)
    g:setCenter(col1, center4)
    return g
  end)()

  self.curveReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = etcher:getParameter("EditCurve")
    g:setParameter(param)
    g:setAttributes(app.unitNone, curveMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    return g
  end)()

  self.weightReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = etcher:getParameter("EditWeight")
    g:setParameter(param)
    g:setAttributes(app.unitNone, weightMap)
    g:setPrecision(1)
    g:setCenter(col3, center4)
    return g
  end)()

  self.curveLabel = app.Label("lin", 10)
  self.curveLabel:fitToText(0)
  self.curveLabel:setCenter(col2, center3 + 1)

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
  self.subGraphic:addChild(self.offsetReadout)
  self.subGraphic:addChild(self.curveReadout)
  self.subGraphic:addChild(self.weightReadout)
  self.subGraphic:addChild(self.curveLabel)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("offset", 1))
  self.subGraphic:addChild(app.SubButton("curve", 2))
  self.subGraphic:addChild(app.SubButton("weight", 3))

  self.pDisplay:follow(etcher)
  self.pDisplay:setEditParam(etcher:getParameter("EditOffset"))
  etcher:loadSegment(0)
  self:updateTitle()
end

function SegmentListControl:updateTitle()
  self.description:setText(string.format("Seg %d", self.currentSegment + 1))
end

function SegmentListControl:updateCurveLabel()
  local val = math.floor(self.curveReadout:getValueInUnits() + 0.5)
  local name = curveNames[val]
  if name then
    self.curveLabel:setText(name)
  end
end

function SegmentListControl:switchToSegment(newSeg)
  local segCount = self.etcher:getSegmentCount()
  newSeg = math.max(0, math.min(segCount - 1, newSeg))
  if newSeg == self.currentSegment then return end

  self.etcher:storeSegment(self.currentSegment)
  self.currentSegment = newSeg
  self.etcher:loadSegment(newSeg)
  self.pDisplay:setSelectedSegment(newSeg)
  self:updateTitle()
  self:updateCurveLabel()
end

function SegmentListControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function SegmentListControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function SegmentListControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function SegmentListControl:doKeyboardSet(args)
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

function SegmentListControl:subReleased(i, shifted)
  if shifted then return false end
  local args = nil
  if i == 1 then
    args = { selected = self.offsetReadout, message = "Segment offset (-5 to 5).", commit = "Updated offset." }
  elseif i == 2 then
    args = { selected = self.curveReadout, message = "Curve type (0=step, 1=linear, 2=cubic).", commit = "Updated curve." }
  elseif i == 3 then
    args = { selected = self.weightReadout, message = "Segment weight (0.1 to 4.0).", commit = "Updated weight." }
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

function SegmentListControl:scrollSegment(change)
  self.scrollAccum = self.scrollAccum + change
  local steps = math.floor(self.scrollAccum)
  if steps ~= 0 then
    self.scrollAccum = self.scrollAccum - steps
    self:switchToSegment(self.currentSegment + steps)
  end
end

function SegmentListControl:encoder(change, shifted)
  if self.focusedReadout and shifted then
    -- Shift held: scroll segments, keep readout focus
    self:scrollSegment(change)
    return true
  elseif self.focusedReadout then
    -- Normal: edit focused param, store immediately for live update
    self.focusedReadout:encoder(change, false, self.encoderState == Encoder.Fine)
    self.etcher:storeSegment(self.currentSegment)
    if self.focusedReadout == self.curveReadout then
      self:updateCurveLabel()
    end
    return true
  else
    -- No focus: scroll segment list with fine control
    self:scrollSegment(change)
    return true
  end
end

function SegmentListControl:upReleased(shifted)
  if self.focusedReadout then
    -- Step 1: readout focused -> return to list scroll mode
    self.focusedReadout = nil
    self:setSubCursorController(nil)
    return true
  elseif self:hasFocus("encoder") then
    -- Step 2: list scroll mode -> release focus entirely
    self:unfocus()
    return true
  end
  return false
end

function SegmentListControl:reconcileSelection()
  local segCount = self.etcher:getSegmentCount()
  if self.currentSegment >= segCount then
    local clamped = math.max(0, segCount - 1)
    self.currentSegment = clamped
    self.etcher:loadSegment(clamped)
    self.pDisplay:setSelectedSegment(clamped)
    self:updateTitle()
  end
end

function SegmentListControl:onCursorEnter(spot)
  Base.onCursorEnter(self, spot)
  self:reconcileSelection()
  self.pDisplay:setFocused(true)
end

function SegmentListControl:onCursorLeave(spot)
  self.pDisplay:setFocused(false)
  Base.onCursorLeave(self, spot)
end

return SegmentListControl
