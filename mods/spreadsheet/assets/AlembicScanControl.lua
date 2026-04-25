-- AlembicScanControl: paramMode shift-toggle scan control for Alembic.
-- Headline knob = scan position; shift-toggle reveals K (path window) on
-- the sub-display. Structurally identical to ColmatageBlockControl per
-- feedback_identical_means_identical, with one deliberate simplification:
-- a single centered K readout + middle SubButton instead of the canonical
-- 3-column layout (only one parameter to multiplex).
--
-- setFocusedReadout always called via the method, never direct assignment,
-- per feedback_gainbias_dual_mode_focus.

local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col2 = app.BUTTON2_CENTER

local AlembicScanControl = Class {}
AlembicScanControl:include(GainBias)

function AlembicScanControl:init(args)
  GainBias.init(self, args)

  -- Sphere viz attaches as the main cursor controller (mirrors
  -- SomScanControl + ColmatageBlockControl pattern).
  local sphere = libspreadsheet.AlembicSphereGraphic(0, 0, ply, 64)
  if args.op then sphere:follow(args.op) end
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(sphere)
  self:setMainCursorController(sphere)
  self:setControlGraphic(container)

  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local kMap = (function()
    local m = app.LinearDialMap(2, 6)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  -- Single centered readout (deviation from Colmatage's 3-column layout;
  -- only one parameter to multiplex here).
  self.kReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.kParam)
    g:setAttributes(app.unitNone, kMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    return g
  end)()

  local desc = app.Label("Path window K", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)

  self.paramSubGraphic:addChild(self.kReadout)
  self.paramSubGraphic:addChild(desc)
  -- Middle sub button (index 2) to align with the centered readout.
  self.paramSubGraphic:addChild(app.SubButton("K", 2))
end

function AlembicScanControl:setParamMode(enabled)
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

function AlembicScanControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    self:setSubCursorController(self.paramModeDefaultSub)
  end
end

function AlembicScanControl:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function AlembicScanControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function AlembicScanControl:shiftReleased()
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

function AlembicScanControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function AlembicScanControl:subReleased(i, shifted)
  if self.paramMode then
    local readout, label
    if i == 2 then readout, label = self.kReadout, "K" end
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

function AlembicScanControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function AlembicScanControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function AlembicScanControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return AlembicScanControl
