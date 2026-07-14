-- Fabula overview control: a GainBias whose main dial is Size and whose fader
-- graphic is replaced by the FabricGraphic waterfall viz. Tap-shift toggles a
-- param submenu exposing Decay / Damp / Diffusion (each a Readout bound to the
-- real Bias parameter). Mirrors spreadsheet/HelicaseOverviewControl.lua.

local app = app
local libzaum = require "zaum.libzaum"
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "zaum.ShiftHelpers"

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
  local fabric = libzaum.FabricGraphic(0, 0, ply, 64)
  fabric:follow(args.tank)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(fabric)
  self:setMainCursorController(fabric)
  self:setControlGraphic(container)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.levelSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Overview", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.paramSubGraphic:addChild(desc)

  self.decayReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.decayParam)
    g:setAttributes(app.unitNone, zeroOneMap())
    g:setPrecision(2)
    g:setCenter(col1, center4)
    return g
  end)()
  self.paramSubGraphic:addChild(self.decayReadout)

  self.dampReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.dampParam)
    g:setAttributes(app.unitNone, zeroOneMap())
    g:setPrecision(2)
    g:setCenter(col2, center4)
    return g
  end)()
  self.paramSubGraphic:addChild(self.dampReadout)

  self.diffReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.diffusionParam)
    g:setAttributes(app.unitNone, zeroOneMap())
    g:setPrecision(2)
    g:setCenter(col3, center4)
    return g
  end)()
  self.paramSubGraphic:addChild(self.diffReadout)

  self.paramSubGraphic:addChild(app.SubButton("dcy", 1))
  self.paramSubGraphic:addChild(app.SubButton("damp", 2))
  self.paramSubGraphic:addChild(app.SubButton("diff", 3))

  -- Default sub focused when the shift submenu is opened.
  self.paramModeDefaultSub = self.decayReadout

  -- Default: Size on the main encoder (level sub). Tap-shift reveals Decay/Damp/Diff.
  self:setParamMode(false)
end

-- Single source of truth: points BOTH the encoder-routing field
-- (paramFocusedReadout) and the rendered sub-caret (setSubCursorController) at the
-- SAME readout. Grabs encoder focus FIRST so setSubCursorController actually pushes
-- to the renderer (Base/Widget only notifies when the focused widget == self);
-- setSubCursorController is LAST so it wins over the self.bias cursor that
-- setFocusedReadout(self.bias) re-installs (feedback_subcursor_inheritance).
function FabulaOverviewControl:focusParamSub(readout)
  self:setFocusedReadout(self.bias)   -- keep GainBias's focusedReadout valid+non-nil
  readout:save()
  self.paramFocusedReadout = readout
  if not self:hasFocus("encoder") then self:focus() end
  self:setSubCursorController(readout) -- LAST: caret follows the encoder
end

function FabulaOverviewControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  if enabled then
    -- Attach the param sub FIRST so its readouts are live when the cursor installs.
    self.subGraphic = self.paramSubGraphic
    self:addSubGraphic(self.subGraphic)
    self:focusParamSub(self.paramModeDefaultSub)  -- caret + encoder both on S1
  else
    self.subGraphic = self.levelSubGraphic
    self.paramFocusedReadout = nil
    self:addSubGraphic(self.subGraphic)
    self:setFocusedReadout(self.bias)             -- Size on encoder, caret on bias
  end
end

function FabulaOverviewControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    -- STRICT convention on re-entry: clear the sub cursor and leave the encoder on
    -- Size (paramFocusedReadout was niled in onCursorLeave). No focus-grab on enter
    -- (that broke navigation), no phantom caret. Tap a SubButton to edit Decay/Damp/
    -- Diff. The ideal "auto-focus the shown sub with a caret" needs deeper research
    -- into the renderer's focus==self gate -> see fabula-overview-caret TODO.
    self:setSubCursorController(nil)
  end
end

function FabulaOverviewControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setFocusedReadout(self.bias)   -- keep GainBias.onFocused safe on return
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

function FabulaOverviewControl:focusReadout(readout)
  self:focusParamSub(readout)   -- one path for caret + encoder
end

function FabulaOverviewControl:subReleased(i, shifted)
  if self.paramMode then
    if i == 1 then
      if shifted then ShiftHelpers.openKeyboardFor(self.decayReadout, "decay")
      else self:focusReadout(self.decayReadout) end
    elseif i == 2 then
      if shifted then ShiftHelpers.openKeyboardFor(self.dampReadout, "damp")
      else self:focusReadout(self.dampReadout) end
    elseif i == 3 then
      if shifted then ShiftHelpers.openKeyboardFor(self.diffReadout, "diffusion")
      else self:focusReadout(self.diffReadout) end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function FabulaOverviewControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
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
