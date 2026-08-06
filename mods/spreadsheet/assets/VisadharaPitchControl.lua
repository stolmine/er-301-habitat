local app = app
local Class = require "Base.Class"
local Pitch = require "Unit.ViewControl.Pitch"
local Encoder = require "Encoder"
local DiscreteStep = require "spreadsheet.DiscreteStep"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col2 = app.BUTTON2_CENTER

-- Visadhara V/Oct control with octave-select on the shift sub-display.
--
-- Structure mirrors Ngoma's DrumVoicePitchControl (paramMode shift-
-- toggle between stock V/Oct sub and a custom octave sub) plus
-- Rauschen's app.Readout:addThresholdLabel(threshold, "text") pattern
-- for the Bass / Alto / Tenor text labels.
--
-- The underlying octave parameter is an od::Parameter (CV-able)
-- driven by a ParameterAdapter. DialMap snaps to integers 1..3; the
-- threshold labels override the numeric display of the Readout when
-- the value crosses 1.0 / 1.5 / 2.5. Same threshold table is reused
-- by the expanded-view ThresholdFader (see Visadhara.lua).

local VisadharaPitchControl = Class {}
VisadharaPitchControl:include(Pitch)

function VisadharaPitchControl:init(args)
  Pitch.init(self, args)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  -- Integer-snap DialMap. Coarse encoder step = 1 (one integer per
  -- click — flips Bass↔Alto↔Tenor). Fine step (shift+encoder) =
  -- 0.1 so user can dial precisely between threshold boundaries
  -- without accidentally skipping past the target value via
  -- encoder acceleration. setRounding(1) keeps the readout's
  -- displayed text snapped to nearest integer; underlying
  -- parameter can hover between for CV-modulation purposes.
  local octaveMap = (function()
    local m = app.LinearDialMap(1, 3)
    m:setSteps(1, 0.1, 0.01, 0.01)
    m:setRounding(1)
    return m
  end)()

  self.octaveReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.octaveParam)
    g:setAttributes(app.unitNone, octaveMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    return g
  end)()

  -- Threshold labels: as the parameter value crosses each threshold the
  -- Readout's display text flips to the next label. Inclusive lower
  -- bounds: [1.0, 1.5) → "Bass", [1.5, 2.5) → "Alto", [2.5, ∞) → "Tenor".
  if self.octaveReadout.addThresholdLabel then
    self.octaveReadout:addThresholdLabel(1.0, "Bass")
    self.octaveReadout:addThresholdLabel(1.5, "Alto")
    self.octaveReadout:addThresholdLabel(2.5, "Tenor")
  end

  local desc = app.Label("Octave", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  self.paramSubGraphic:addChild(self.octaveReadout)
  self.paramSubGraphic:addChild(desc)
  self.paramSubGraphic:addChild(app.SubButton("oct", 2))
end

function VisadharaPitchControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.paramSubGraphic
  else
    self.subGraphic = self.normalSubGraphic
  end
  self:addSubGraphic(self.subGraphic)
end

function VisadharaPitchControl:onCursorEnter(spot)
  Pitch.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    self:setSubCursorController(self.paramModeDefaultSub)
  end
end

function VisadharaPitchControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return Pitch.spotReleased(self, spot, shifted)
end

function VisadharaPitchControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  Pitch.onCursorLeave(self, spot)
end

function VisadharaPitchControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function VisadharaPitchControl:shiftReleased()
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

function VisadharaPitchControl:subReleased(i, shifted)
  if self.paramMode then
    if i == 2 then
      if shifted then
        ShiftHelpers.openKeyboardFor(self.octaveReadout, "octave")
      else
        self.octaveReadout:save()
        self.paramFocusedReadout = self.octaveReadout
        DiscreteStep.reset(self)
        self:setSubCursorController(self.octaveReadout)
        if not self:hasFocus("encoder") then self:focus() end
      end
    end
    return true
  end
  return Pitch.subReleased(self, i, shifted)
end

function VisadharaPitchControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    if self.paramFocusedReadout == self.octaveReadout then
      -- enumerated set, not a magnitude: steps whole entries under the
      -- discrete standard so a fast turn cannot skip past one.
      DiscreteStep.encoder(self, self.octaveReadout, change, 1, 3)
    else
      self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    end
    return true
  end
  return Pitch.encoder(self, change, shifted)
end

function VisadharaPitchControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return Pitch.zeroPressed(self)
end

function VisadharaPitchControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return Pitch.cancelReleased(self, shifted)
end

return VisadharaPitchControl
