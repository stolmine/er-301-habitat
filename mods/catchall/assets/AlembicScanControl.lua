-- AlembicScanControl: paramMode shift-toggle scan control for Alembic.
-- Headline knob = scan position; shift-toggle reveals two readouts on the
-- sub-display: K (path window, sub1) and sample-pointer depth (Phase 8e,
-- sub3). Structurally close to ColmatageBlockControl per
-- feedback_identical_means_identical.
--
-- setFocusedReadout always called via the method, never direct assignment,
-- per feedback_gainbias_dual_mode_focus.

local app = app
local libcatchall = require "catchall.libcatchall"
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "catchall.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col3 = app.BUTTON3_CENTER

local AlembicScanControl = Class {}
AlembicScanControl:include(GainBias)

function AlembicScanControl:init(args)
  GainBias.init(self, args)

  -- Sphere viz attaches as the main cursor controller (mirrors
  -- SomScanControl + ColmatageBlockControl pattern).
  local sphere = libcatchall.AlembicSphereGraphic(0, 0, ply, 64)
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

  -- Two readouts in the canonical paramMode layout: sub1 (left) = K
  -- (path window), sub3 (right) = sample-pointer excitation depth.
  self.kReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.kParam)
    g:setAttributes(app.unitNone, kMap)
    g:setPrecision(0)
    g:setCenter(col1, center4)
    return g
  end)()

  self.depthReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.depthParam)
    g:setAttributes(app.unitNone, Encoder.getMap("[0,1]"))
    g:setPrecision(2)
    g:setCenter(col3, center4)
    return g
  end)()

  local descK = app.Label("Path window K", 10)
  descK:fitToText(3)
  descK:setSize(ply * 3, descK.mHeight)
  descK:setBorder(1)
  descK:setCornerRadius(3, 0, 0, 3)
  descK:setCenter(col1, center1 + 1)

  local descD = app.Label("Sample pointer depth", 10)
  descD:fitToText(3)
  descD:setSize(ply * 3, descD.mHeight)
  descD:setBorder(1)
  descD:setCornerRadius(3, 0, 0, 3)
  descD:setCenter(col3, center1 + 1)

  self.paramSubGraphic:addChild(self.kReadout)
  self.paramSubGraphic:addChild(self.depthReadout)
  self.paramSubGraphic:addChild(descK)
  self.paramSubGraphic:addChild(descD)
  self.paramSubGraphic:addChild(app.SubButton("K", 1))
  self.paramSubGraphic:addChild(app.SubButton("dpth", 3))
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
    if i == 1 then readout, label = self.kReadout, "K"
    elseif i == 3 then readout, label = self.depthReadout, "Depth" end
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
