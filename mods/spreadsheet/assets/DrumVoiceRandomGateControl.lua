-- DrumVoiceRandomGateControl -- button-triggered random-fire ply for Ngoma.
--
-- Static sub-display (Pattern B): depth, spread, fire!
--   sub1 tap: focus depth readout (encoder edits depth).
--   sub2 tap: focus spread readout (encoder edits spread).
--   sub3 tap: invoke fire() callback -- applies random perturbation to
--             Ngoma's tone-shaping params (Character/Shape/Grit/Punch/
--             Sweep/SweepTime/Decay/Hold/Attack) scaled by depth/spread.
--
-- No CV trigger input in v1 -- button-only fire. CV rising-edge dispatch
-- is deferred (would require either a C++-side mTransform inlet polled in
-- the DSP with a Lua-visible flag, or a Lua-polled signal from the
-- Comparator. See planning/drum-voice.md risks.)

local app = app
local Class = require "Base.Class"
local Base = require "Unit.ViewControl.EncoderControl"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local line1 = app.GRID5_LINE1
local line4 = app.GRID5_LINE4
local center1 = app.GRID5_CENTER1
local center3 = app.GRID5_CENTER3
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

-- Main-graphic instructions: draws an "R" arrow loop suggesting
-- randomization. Simple analog-style sketch with no external DSP state.
local mainInstructions = app.DrawingInstructions()
mainInstructions:circle(col2, center3, 12)
mainInstructions:hline(col1 + 8, col3 - 12, center3 - 6)
mainInstructions:triangle(col3 - 14, center3 - 6, 0, 3)
mainInstructions:hline(col1 + 12, col3 - 8, center3 + 6)
mainInstructions:triangle(col1 + 10, center3 + 6, 180, 3)

local DrumVoiceRandomGateControl = Class {
  type = "DrumVoiceRandomGateControl",
  canEdit = false,
  canMove = true
}
DrumVoiceRandomGateControl:include(Base)

function DrumVoiceRandomGateControl:init(args)
  local button = args.button or "xform"
  local description = args.description or "Randomize"
  local fire = args.fire or app.logError("%s.init: fire callback missing.", self)
  local depthParam = args.depthParam or app.logError("%s.init: depthParam missing.", self)
  local spreadParam = args.spreadParam or app.logError("%s.init: spreadParam missing.", self)

  Base.init(self, button)
  self:setClassName("DrumVoiceRandomGateControl")

  self.fire = fire

  -- Main graphic: token visualization of the randomize action.
  local graphic = app.Graphic(0, 0, ply, 64)
  local drawing = app.Drawing(0, 0, ply, 64)
  drawing:add(mainInstructions)
  graphic:addChild(drawing)

  local label = app.Label(description, 10)
  label:fitToText(3)
  label:setSize(ply, label.mHeight)
  label:setBorder(1)
  label:setCornerRadius(3, 3, 3, 3)
  label:setCenter(col2, line4 + 8)
  graphic:addChild(label)

  self:setMainCursorController(graphic)
  self:setControlGraphic(graphic)
  self:addSpotDescriptor { center = 0.5 * ply }

  -- Sub-display: depth / spread / fire!
  local subGraphic = app.Graphic(0, 0, 128, 64)

  local unitMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  local function makeReadout(param, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(app.unitNone, unitMap)
    g:setPrecision(2)
    g:setCenter(x, center4)
    return g
  end

  self.depthReadout = makeReadout(depthParam, col1)
  self.spreadReadout = makeReadout(spreadParam, col2)

  local desc = app.Label("Depth / Spread / Fire", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  subGraphic:addChild(self.depthReadout)
  subGraphic:addChild(self.spreadReadout)
  subGraphic:addChild(desc)
  subGraphic:addChild(app.SubButton("depth", 1))
  subGraphic:addChild(app.SubButton("spread", 2))
  subGraphic:addChild(app.SubButton("fire!", 3))

  self.subGraphic = subGraphic
  self.focusedReadout = nil
end

function DrumVoiceRandomGateControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function DrumVoiceRandomGateControl:subPressed(i, shifted)
  if shifted then return false end
  if i == 3 then
    self.fire()
  end
  return true
end

function DrumVoiceRandomGateControl:subReleased(i, shifted)
  if i == 1 then
    if shifted then
      ShiftHelpers.openKeyboardFor(self.depthReadout, "depth")
    else
      if self:hasFocus("encoder") then
        self:setFocusedReadout(self.depthReadout)
      else
        self:focus()
        self:setFocusedReadout(self.depthReadout)
      end
    end
    return true
  elseif i == 2 then
    if shifted then
      ShiftHelpers.openKeyboardFor(self.spreadReadout, "spread")
    else
      if self:hasFocus("encoder") then
        self:setFocusedReadout(self.spreadReadout)
      else
        self:focus()
        self:setFocusedReadout(self.spreadReadout)
      end
    end
    return true
  elseif i == 3 then
    -- Fire handled in subPressed; nothing on release.
    return true
  end
  return Base.subReleased(self, i, shifted)
end

function DrumVoiceRandomGateControl:spotReleased(spot, shifted)
  self.focusedReadout = nil
  self:setSubCursorController(nil)
  return Base.spotReleased(self, spot, shifted)
end

function DrumVoiceRandomGateControl:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return false
end

function DrumVoiceRandomGateControl:zeroPressed()
  if self.focusedReadout then
    self.focusedReadout:zero()
    return true
  end
  return false
end

function DrumVoiceRandomGateControl:cancelReleased(shifted)
  if self.focusedReadout then
    self.focusedReadout:restore()
    return true
  end
  return false
end

function DrumVoiceRandomGateControl:upReleased(shifted)
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

return DrumVoiceRandomGateControl
