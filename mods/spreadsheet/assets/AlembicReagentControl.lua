-- AlembicReagentControl: paramMode shift-toggle reagent control for
-- Alembic. Headline knob = reagent scan position (independent of the
-- main scan -- moves through the wavetable transfer-function frames);
-- shift-toggle reveals the Amount fader on the sub-display. Defaults
-- to amount=0 so a freshly-loaded unit is clean PM until the user
-- dials the wavetable in.
--
-- Structurally identical to AlembicScanControl per
-- feedback_identical_means_identical, with two deviations: no sphere
-- viz attached to the main cursor (the sphere belongs to the main
-- scan ply), and the shift-toggle parameter is the Amount fader
-- instead of K.

local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col2 = app.BUTTON2_CENTER

local AlembicReagentControl = Class {}
AlembicReagentControl:include(GainBias)

function AlembicReagentControl:init(args)
  GainBias.init(self, args)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local amountMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.05, 0.01, 0.001)
    m:setRounding(0.001)
    return m
  end)()

  -- Single centered readout (matches AlembicScanControl's K layout).
  self.amountReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.amountParam)
    g:setAttributes(app.unitNone, amountMap)
    g:setPrecision(3)
    g:setCenter(col2, center4)
    return g
  end)()

  local desc = app.Label("Amount", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  self.paramSubGraphic:addChild(self.amountReadout)
  self.paramSubGraphic:addChild(desc)
  self.paramSubGraphic:addChild(app.SubButton("amt", 2))
end

function AlembicReagentControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.paramSubGraphic
  else
    self.subGraphic = self.normalSubGraphic
    self:setFocusedReadout(self.bias)
  end
  self:addSubGraphic(self.subGraphic)
end

function AlembicReagentControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    self:setSubCursorController(self.paramModeDefaultSub)
  end
end

function AlembicReagentControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function AlembicReagentControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function AlembicReagentControl:shiftReleased()
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

function AlembicReagentControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function AlembicReagentControl:subReleased(i, shifted)
  if self.paramMode then
    local readout, label
    if i == 2 then readout, label = self.amountReadout, "Amount" end
    if readout then
      if shifted then
        ShiftHelpers.openKeyboardFor(readout, label)
        return true
      end
      readout:save()
      self.paramFocusedReadout = readout
      self:setSubCursorController(readout)
      if not self:hasFocus("encoder") then self:focus() end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function AlembicReagentControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function AlembicReagentControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function AlembicReagentControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return AlembicReagentControl
