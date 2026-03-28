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

local function intMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(2, 1, 0.25, 0.25)
  map:setRounding(1)
  return map
end

local function floatMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.25, 0.1, 0.01, 0.001)
  return map
end

local lengthMap = intMap(1, 16)
local velocityMap = floatMap(0, 1)

local ChaselightControl = Class {
  type = "ChaselightControl",
  canEdit = false,
  canMove = true
}
ChaselightControl:include(Base)

function ChaselightControl:init(args)
  local description = args.description or "Steps"
  local seq = args.seq or app.logError("%s.init: seq is missing.", self)

  Base.init(self, "steps")
  self:setClassName("ChaselightControl")

  local width = args.width or ply

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.ChaselightGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * ply }

  self.seq = seq
  self.currentStep = 0
  self.scrollAccum = 0

  -- On/off label (not a readout - toggled directly)
  self.gateLabel = app.Label("OFF", 10)
  self.gateLabel:fitToText(0)
  self.gateLabel:setCenter(col1, center3 + 1)

  -- Length readout
  self.lengthReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = seq:getParameter("EditLength")
    g:setParameter(param)
    g:setAttributes(app.unitNone, lengthMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    return g
  end)()

  -- Velocity readout
  self.velocityReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = seq:getParameter("EditVelocity")
    g:setParameter(param)
    g:setAttributes(app.unitNone, velocityMap)
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
  self.subGraphic:addChild(self.gateLabel)
  self.subGraphic:addChild(self.lengthReadout)
  self.subGraphic:addChild(self.velocityReadout)
  self.subGraphic:addChild(self.description)
  self.subGraphic:addChild(app.SubButton("on/off", 1))
  self.subGraphic:addChild(app.SubButton("length", 2))
  self.subGraphic:addChild(app.SubButton("vel", 3))

  self.pDisplay:follow(seq)
  self.pDisplay:setEditParam(seq:getParameter("EditGate"))
  seq:loadStep(0)
  self:updateTitle()
  self:updateGateLabel()
end

function ChaselightControl:updateTitle()
  self.description:setText(string.format("Step %d", self.currentStep + 1))
end

function ChaselightControl:updateGateLabel()
  local isOn = self.seq:getStepGate(self.currentStep)
  self.gateLabel:setText(isOn and "ON" or "OFF")
end

function ChaselightControl:switchToStep(newStep)
  local seqLen = self.seq:getSeqLength()
  newStep = math.max(0, math.min(seqLen - 1, newStep))
  if newStep == self.currentStep then return end

  self.seq:storeStep(self.currentStep)
  self.currentStep = newStep
  self.seq:loadStep(newStep)
  self.pDisplay:setSelectedStep(newStep)
  self:updateTitle()
  self:updateGateLabel()
end

function ChaselightControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function ChaselightControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function ChaselightControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function ChaselightControl:doKeyboardSet(args)
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

function ChaselightControl:subReleased(i, shifted)
  if shifted then return false end

  if i == 1 then
    -- Toggle on/off immediately
    local isOn = self.seq:getStepGate(self.currentStep)
    self.seq:setStepGate(self.currentStep, not isOn)
    self.seq:loadStep(self.currentStep) -- refresh edit buffer
    self:updateGateLabel()
    return true
  end

  local args = nil
  if i == 2 then
    args = { selected = self.lengthReadout, message = "Gate length (1-16 ticks).", commit = "Updated length." }
  elseif i == 3 then
    args = { selected = self.velocityReadout, message = "Velocity (0-1).", commit = "Updated velocity." }
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

function ChaselightControl:scrollStep(change)
  self.scrollAccum = self.scrollAccum + change
  local steps = math.floor(self.scrollAccum)
  if steps ~= 0 then
    self.scrollAccum = self.scrollAccum - steps
    self:switchToStep(self.currentStep + steps)
  end
end

function ChaselightControl:encoder(change, shifted)
  if self.focusedReadout and shifted then
    -- Shift held: scroll steps, keep readout focus
    self:scrollStep(change)
    return true
  elseif self.focusedReadout then
    -- Normal: edit focused param, store immediately
    self.focusedReadout:encoder(change, false, self.encoderState == Encoder.Coarse)
    self.seq:storeStep(self.currentStep)
    return true
  else
    -- No focus: scroll steps with fine control
    self:scrollStep(change)
    return true
  end
end

function ChaselightControl:upReleased(shifted)
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

return ChaselightControl
