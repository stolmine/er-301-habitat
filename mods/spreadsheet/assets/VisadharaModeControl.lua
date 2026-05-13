local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
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

-- Visadhara Mode control with spread / harmonic / morph as paramMode
-- shift sub-readouts. Mode is the headline control: it carries the
-- Skin↔Liquid↔Metal crossfade on the main fader AND surfaces the
-- three timbral macros (spread, harmonic, morph) one tap away.
--
-- paramMode defaults to TRUE — the custom sub IS the primary view
-- (subs cleanly expose the timbral character). Shift toggles to
-- the stock GainBias sub (level / scale readouts for Mode).
--
-- Structurally identical to RauschenCutoffControl (paramMode +
-- multi-readout sub-graphic), just with 3 readouts instead of 2.
--
-- Phase 3 will replace the main fader area with a custom viz
-- Graphic. For now the stock fader stays.

local VisadharaModeControl = Class {}
VisadharaModeControl:include(GainBias)

function VisadharaModeControl:init(args)
  GainBias.init(self, args)

  -- Phase 3a Corona viz on the main fader area. Replaces the stock
  -- GainBias fader. The graphic reads decimated audio output from
  -- the Visadhara op via getVizSample (mpVisadhara pointer set via
  -- follow). Encoder + cursor still target the Mode bias parameter
  -- (setMainCursorController on the graphic routes interaction).
  if args.visadhara then
    local corona = libspreadsheet.VisadharaCoronaGraphic(0, 0, ply, 64)
    corona:follow(args.visadhara)
    local container = app.Graphic(0, 0, ply, 64)
    container:addChild(corona)
    self:setMainCursorController(corona)
    self:setControlGraphic(container)
  end

  self.paramMode = true
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local function makeReadout(param, map, precision, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(app.unitNone, map)
    g:setPrecision(precision)
    g:setCenter(x, center4)
    return g
  end

  local unitMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.0001)
    return m
  end)()

  self.spreadReadout   = makeReadout(args.spreadParam,   unitMap, 3, col1)
  self.harmonicReadout = makeReadout(args.harmonicParam, unitMap, 3, col2)
  self.morphReadout    = makeReadout(args.morphParam,    unitMap, 3, col3)

  -- Default sub on paramMode entry — leftmost readout (spread).
  -- Drives the bias-bound sub-cursor highlight per Decision 8 of
  -- the shift-handling audit. See feedback_subcursor_inheritance
  -- for the renderer mechanics; for non-bias-bound subs (none of
  -- ours share param with self.bias) the highlight currently does
  -- not render visibly but encoder routing still works.
  self.paramModeDefaultSub = self.spreadReadout

  local desc = app.Label("Spread / Harm / Morph", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(app.BUTTON1_CENTER + ply, center1 + 1)

  self.paramSubGraphic:addChild(self.spreadReadout)
  self.paramSubGraphic:addChild(self.harmonicReadout)
  self.paramSubGraphic:addChild(self.morphReadout)
  self.paramSubGraphic:addChild(desc)
  self.paramSubGraphic:addChild(app.SubButton("sprd", 1))
  self.paramSubGraphic:addChild(app.SubButton("harm", 2))
  self.paramSubGraphic:addChild(app.SubButton("mrph", 3))

  -- Start in paramMode (custom subs visible). User shifts to flip
  -- to the stock GainBias sub for level/scale of the Mode param
  -- itself.
  self:removeSubGraphic(self.subGraphic)
  self.subGraphic = self.paramSubGraphic
  self:addSubGraphic(self.subGraphic)
end

function VisadharaModeControl:setParamMode(enabled)
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

function VisadharaModeControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    self:setSubCursorController(self.paramModeDefaultSub)
  end
end

function VisadharaModeControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function VisadharaModeControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function VisadharaModeControl:shiftReleased()
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

function VisadharaModeControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function VisadharaModeControl:subReleased(i, shifted)
  if self.paramMode then
    local readout, label
    if     i == 1 then readout, label = self.spreadReadout,   "spread"
    elseif i == 2 then readout, label = self.harmonicReadout, "harmonic"
    elseif i == 3 then readout, label = self.morphReadout,    "morph"
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

function VisadharaModeControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function VisadharaModeControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function VisadharaModeControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return VisadharaModeControl
