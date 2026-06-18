-- MirrorOverviewControl
--
-- Overview ply for the Mirror unit. Primary control is Shape (the
-- wavetable position). Shift-toggle exposes a 3-readout paramSubGraphic
-- holding Fundamental / Formant / Feedback so all three secondary
-- frequency/modulation knobs share one ply slot.
--
-- Follows the spreadsheet paramMode convention (see
-- feedback_parammode_convention + planning/shift-handling.md):
-- decisions 1-8 locked. Modeled directly on ParfaitMixControl.
--
-- Bias (Shape) is distinct from all three sub readouts, so
-- paramModeDefaultSub is intentionally unset (decision 8: only
-- bias-bound sub1 controls declare it).

local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local MirrorOverviewControl = Class {}
MirrorOverviewControl:include(GainBias)

function MirrorOverviewControl:init(args)
  GainBias.init(self, args)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local function makeReadout(param, map, units, precision, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(units, map)
    g:setPrecision(precision)
    g:setCenter(x, center4)
    return g
  end

  local f0Map = (function()
    local m = app.LinearDialMap(0.1, 2000)
    m:setSteps(100, 10, 1, 0.1)
    return m
  end)()

  local fbMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  self.f0Readout       = makeReadout(args.fundamental, f0Map, app.unitHertz, 1, col1)
  self.formantReadout  = makeReadout(args.formant,     f0Map, app.unitHertz, 1, col2)
  self.feedbackReadout = makeReadout(args.feedback,    fbMap, app.unitNone,  2, col3)

  local desc = app.Label("Freq / Form / Fbck", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  self.paramSubGraphic:addChild(self.f0Readout)
  self.paramSubGraphic:addChild(self.formantReadout)
  self.paramSubGraphic:addChild(self.feedbackReadout)
  self.paramSubGraphic:addChild(desc)
  self.paramSubGraphic:addChild(app.SubButton("freq", 1))
  self.paramSubGraphic:addChild(app.SubButton("form", 2))
  self.paramSubGraphic:addChild(app.SubButton("fbck", 3))
end

function MirrorOverviewControl:setParamMode(enabled)
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

function MirrorOverviewControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    self:setSubCursorController(self.paramModeDefaultSub)
  end
end

function MirrorOverviewControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function MirrorOverviewControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function MirrorOverviewControl:shiftReleased()
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

function MirrorOverviewControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function MirrorOverviewControl:subReleased(i, shifted)
  if self.paramMode then
    local readout, label
    if     i == 1 then readout, label = self.f0Readout,       "freq"
    elseif i == 2 then readout, label = self.formantReadout,  "formant"
    elseif i == 3 then readout, label = self.feedbackReadout, "feedback"
    end
    if readout then
      if shifted then
        ShiftHelpers.openKeyboardFor(readout, label)
      else
        readout:save()
        self.paramFocusedReadout = readout
        self:setSubCursorController(readout)
        if not self:hasFocus("encoder") then self:focus() end
      end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function MirrorOverviewControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function MirrorOverviewControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function MirrorOverviewControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return MirrorOverviewControl
