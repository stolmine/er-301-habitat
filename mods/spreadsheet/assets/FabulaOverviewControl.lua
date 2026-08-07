-- Fabula overview control: a GainBias whose main dial is Size and whose fader
-- graphic is replaced by the FabricGraphic waterfall viz. Tap-shift toggles a
-- param submenu exposing Decay / Damp / Diffusion (each a Readout bound to the
-- real Bias parameter).
--
-- Cursor / paramMode / shift / enter handling is a VERBATIM port of Pecto's
-- spreadsheet/DensityControl.lua (the only differences are the three readouts
-- and the fabric viz replacing the fader). Aped exactly so the subdisplay cursor
-- and enter-to-expand behave identically to Pecto (previous custom focusParamSub
-- logic caused the caret weirdness - see the retired fabula-overview-caret note).

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

local FabulaOverviewControl = Class {}
FabulaOverviewControl:include(GainBias)

local function zeroOneMap()
  local m = app.LinearDialMap(0, 1)
  m:setSteps(0.1, 0.01, 0.001, 0.001)
  return m
end

function FabulaOverviewControl:init(args)
  GainBias.init(self, args)

  -- Replace the fader with the fabric waterfall viz (follows the APFTank op).
  local fabric = libspreadsheet.FabricGraphic(0, 0, ply, 64)
  fabric:follow(args.tank)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(fabric)
  self:setMainCursorController(fabric)
  self:setControlGraphic(container)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local function makeReadout(param, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(app.unitNone, zeroOneMap())
    g:setPrecision(2)
    g:setCenter(x, center4)
    return g
  end

  self.decayReadout = makeReadout(args.decayParam, col1)
  self.dampReadout = makeReadout(args.dampParam, col2)
  self.diffReadout = makeReadout(args.diffusionParam, col3)

  -- Sub1 (decay) is the default cursor target on paramMode entry, so re-entering
  -- the ply lands on a readout instead of nothing. Navigation is unaffected: the
  -- sub buttons still retarget freely, and paramFocusedReadout stays nil until a
  -- button is actually pressed, so the encoder keeps editing the main bias until
  -- the user picks a sub. (feedback_subcursor_inheritance)
  self.paramModeDefaultSub = self.decayReadout

  local desc = app.Label("Decay / Damp / Diff", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  self.paramSubGraphic:addChild(self.decayReadout)
  self.paramSubGraphic:addChild(self.dampReadout)
  self.paramSubGraphic:addChild(self.diffReadout)
  self.paramSubGraphic:addChild(desc)
  self.paramSubGraphic:addChild(app.SubButton("dcy", 1))
  self.paramSubGraphic:addChild(app.SubButton("damp", 2))
  self.paramSubGraphic:addChild(app.SubButton("diff", 3))
end

function FabulaOverviewControl:setParamMode(enabled)
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

function FabulaOverviewControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    self:setSubCursorController(self.paramModeDefaultSub)
  end
end

function FabulaOverviewControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function FabulaOverviewControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function FabulaOverviewControl:shiftReleased()
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

function FabulaOverviewControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function FabulaOverviewControl:subReleased(i, shifted)
  if self.paramMode then
    local readout, label
    if i == 1 then readout, label = self.decayReadout, "decay"
    elseif i == 2 then readout, label = self.dampReadout, "damp"
    elseif i == 3 then readout, label = self.diffReadout, "diffusion"
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

function FabulaOverviewControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function FabulaOverviewControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function FabulaOverviewControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return FabulaOverviewControl
