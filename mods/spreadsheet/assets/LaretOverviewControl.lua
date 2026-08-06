local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center3 = app.GRID5_CENTER3
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local LaretOverviewControl = Class {}
LaretOverviewControl:include(GainBias)

function LaretOverviewControl:init(args)
  GainBias.init(self, args)

  local overview = libspreadsheet.LaretOverviewGraphic(0, 0, ply, 64)
  overview:follow(args.op)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(overview)
  self:setMainCursorController(overview)
  self:setControlGraphic(container)

  self.paramMode = true
  self.shiftHeld = false
  self.shiftUsed = false
  self.op = args.op
  self.levelSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Overview", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.paramSubGraphic:addChild(desc)

  local stepCountMap = (function()
    local m = app.LinearDialMap(1, 16)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  local loopMap = (function()
    local m = app.LinearDialMap(1, 16)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  -- Sub1 is the step-advance toggle: lit = random, dark = sequential.
  local stepModeOption = args.op:getOption("StepMode")
  stepModeOption:enableSerialization()
  self.stepModeIndicator = app.BinaryIndicator(0, 24, ply, 32)
  self.stepModeIndicator:setCenter(col1, center3)

  self.stepCountReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.stepCountParam)
    g:setAttributes(app.unitNone, stepCountMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    if g.useHardSet then g:useHardSet() end
    return g
  end)()

  -- Sub1 is a discrete toggle, not a cursor controller, so paramMode entry
  -- lands on the first editable readout instead.
  self.paramModeDefaultSub = self.stepCountReadout

  self.loopReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.loopParam)
    g:setAttributes(app.unitNone, loopMap)
    g:setPrecision(0)
    g:setCenter(col3, center4)
    if g.useHardSet then g:useHardSet() end
    return g
  end)()
  if self.loopReadout.addName then
    -- Index 0 is unreachable (map clamps to [1,16]); placeholder keeps the
    -- integer indices lined up with the displayed value.
    self.loopReadout:addName("--")
    for i = 1, 16 do
      self.loopReadout:addName(tostring(i))
    end
  end

  self.paramSubGraphic:addChild(self.stepModeIndicator)
  self.paramSubGraphic:addChild(self.stepCountReadout)
  self.paramSubGraphic:addChild(self.loopReadout)
  self.paramSubGraphic:addChild(app.SubButton("rand", 1))
  self.paramSubGraphic:addChild(app.SubButton("steps", 2))
  self.paramSubGraphic:addChild(app.SubButton("loop", 3))

  self:setParamMode(true)
end

function LaretOverviewControl:updateStepModeIndicator()
  if self.op and self.op:isRandomStep() then
    self.stepModeIndicator:on()
  else
    self.stepModeIndicator:off()
  end
end

function LaretOverviewControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.paramSubGraphic
    self:updateStepModeIndicator()
  else
    self.subGraphic = self.levelSubGraphic
    self:setFocusedReadout(self.bias)
  end
  self:addSubGraphic(self.subGraphic)
end

function LaretOverviewControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    self:setSubCursorController(self.paramModeDefaultSub)
  end
end

function LaretOverviewControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function LaretOverviewControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function LaretOverviewControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.paramFocusedReadout and self.shiftSnapshot then
      local cur = self.paramFocusedReadout:getValueInUnits()
      if cur ~= self.shiftSnapshot then
        self.shiftHeld = false
        self.shiftSnapshot = nil
        return true
      end
    end
    self:setParamMode(not self.paramMode)
  end
  self.shiftHeld = false
  self.shiftSnapshot = nil
  return true
end

function LaretOverviewControl:focusParamReadout(readout)
  readout:save()
  self.paramFocusedReadout = readout
  self:setSubCursorController(readout)
  if not self:hasFocus("encoder") then self:focus() end
end

function LaretOverviewControl:subReleased(i, shifted)
  if self.paramMode then
    if i == 1 then
      -- step-advance toggle: shift has no keyboard meaning on a discrete toggle
      if shifted then return true end
      self.op:toggleStepMode()
      self:updateStepModeIndicator()
      return true
    end
    local readout, label
    if i == 2 then readout, label = self.stepCountReadout, "steps"
    elseif i == 3 then readout, label = self.loopReadout, "loop"
    end
    if readout then
      if shifted then
        ShiftHelpers.openKeyboardFor(readout, label)
      else
        self:focusParamReadout(readout)
      end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function LaretOverviewControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function LaretOverviewControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function LaretOverviewControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return LaretOverviewControl
