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

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(1, 0.1, 0.01, 0.001)
  return map
end

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(2, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local offsetMap10 = floatMap(-5, 5)
local offsetMap2 = floatMap(-1, 1)
local offsetMap = offsetMap10
local lengthMap = intMap(1, 16)
local deviationMap = floatMap(0, 1)

local StepListControl = Class {
  type = "StepListControl",
  canEdit = false,
  canMove = true
}
StepListControl:include(Base)

function StepListControl:init(args)
  local description = args.description or "Steps"
  local seq = args.seq or app.logError("%s.init: seq is missing.", self)

  Base.init(self, "steps")
  self:setClassName("StepListControl")

  local width = args.width or ply

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.StepListGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * ply }

  self.seq = seq
  self.currentStep = 0

  -- Sub-display readouts bound to edit buffer params
  self.offsetReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = seq:getParameter("EditOffset")
    g:setParameter(param)
    g:setAttributes(app.unitNone, offsetMap)
    g:setPrecision(2)
    g:setCenter(col1, center4)
    return g
  end)()

  self.lengthReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = seq:getParameter("EditLength")
    g:setParameter(param)
    g:setAttributes(app.unitNone, lengthMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    return g
  end)()

  self.deviationReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = seq:getParameter("EditDeviation")
    g:setParameter(param)
    g:setAttributes(app.unitNone, deviationMap)
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

  self.subGraphic = app.Graphic(0, 0, 128, 64)
  self.subGraphic:addChild(self.offsetReadout)
  self.subGraphic:addChild(self.lengthReadout)
  self.subGraphic:addChild(self.deviationReadout)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("offset", 1))
  self.subGraphic:addChild(app.SubButton("length", 2))
  self.subGraphic:addChild(app.SubButton("dev", 3))

  self.pDisplay:follow(seq)
  self.pDisplay:setEditParam(seq:getParameter("EditOffset"))
  seq:loadStep(0)
  self:updateTitle()
end

function StepListControl:setOffsetRange(is10v)
  local map = is10v and offsetMap10 or offsetMap2
  self.offsetReadout:setAttributes(app.unitNone, map)
end

function StepListControl:updateTitle()
  self.description:setText(string.format("Step %d", self.currentStep + 1))
end

function StepListControl:switchToStep(newStep)
  local seqLen = self.seq:getSeqLength()
  newStep = math.max(0, math.min(seqLen - 1, newStep))
  if newStep == self.currentStep then return end

  self.seq:storeStep(self.currentStep)
  self.currentStep = newStep
  self.seq:loadStep(newStep)
  self.pDisplay:setSelectedStep(newStep)
  self:updateTitle()
end

function StepListControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function StepListControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function StepListControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function StepListControl:doKeyboardSet(args)
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

function StepListControl:subReleased(i, shifted)
  if shifted then return false end
  local args = nil
  if i == 1 then
    args = { selected = self.offsetReadout, message = "Step offset (-5 to 5).", commit = "Updated offset." }
  elseif i == 2 then
    args = { selected = self.lengthReadout, message = "Step length (1-16 ticks).", commit = "Updated length." }
  elseif i == 3 then
    args = { selected = self.deviationReadout, message = "Step deviation (0-1).", commit = "Updated deviation." }
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

function StepListControl:encoder(change, shifted)
  if self.focusedReadout and shifted then
    -- Shift held: scroll steps, keep readout focus
    self:switchToStep(self.currentStep + change)
    return true
  elseif self.focusedReadout then
    -- Normal: edit focused param, store immediately for live update
    self.focusedReadout:encoder(change, false, self.encoderState == Encoder.Coarse)
    self.seq:storeStep(self.currentStep)
    return true
  else
    -- No focus: scroll step list
    self:switchToStep(self.currentStep + change)
    return true
  end
end

function StepListControl:upReleased(shifted)
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

return StepListControl
