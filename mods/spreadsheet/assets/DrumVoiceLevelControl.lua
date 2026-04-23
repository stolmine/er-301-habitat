local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1, col2 = app.BUTTON1_CENTER, app.BUTTON2_CENTER

local DrumVoiceLevelControl = Class {}
DrumVoiceLevelControl:include(GainBias)

function DrumVoiceLevelControl:init(args)
  GainBias.init(self, args)

  self.paramMode, self.shiftHeld, self.shiftUsed = false, false, false
  self.normalSubGraphic = self.subGraphic
  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("Clipper / EQ", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.paramSubGraphic:addChild(desc)

  local clipMap = app.LinearDialMap(0, 1)
  clipMap:setSteps(0.1, 0.01, 0.001, 0.001)
  local eqMap = app.LinearDialMap(0, 1)
  eqMap:setSteps(0.1, 0.01, 0.001, 0.001)

  self.clipperReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.clipperParam)
    g:setAttributes(app.unitNone, clipMap)
    g:setPrecision(2)
    g:setCenter(col1, center4)
    return g
  end)()

  self.eqReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.eqParam)
    g:setAttributes(app.unitNone, eqMap)
    g:setPrecision(2)
    g:setCenter(col2, center4)
    return g
  end)()

  self.paramModeDefaultSub = self.clipperReadout

  local sg = self.paramSubGraphic
  sg:addChild(self.clipperReadout)
  sg:addChild(self.eqReadout)
  sg:addChild(app.SubButton("clip", 1))
  sg:addChild(app.SubButton("eq", 2))
end

function DrumVoiceLevelControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode, self.paramFocusedReadout = enabled, nil
  self:setSubCursorController(nil)
  self.subGraphic = enabled and self.paramSubGraphic or self.normalSubGraphic
  if not enabled then self:setFocusedReadout(self.bias) end
  self:addSubGraphic(self.subGraphic)
end

function DrumVoiceLevelControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then self:setSubCursorController(self.paramModeDefaultSub) end
end

function DrumVoiceLevelControl:onCursorLeave(spot)
  if self.paramMode then self.paramFocusedReadout = nil; self:setSubCursorController(nil) end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function DrumVoiceLevelControl:shiftPressed()
  self.shiftHeld, self.shiftUsed = true, false
  self.shiftSnapshot = self.paramFocusedReadout and self.paramFocusedReadout:getValueInUnits() or nil
  return true
end

function DrumVoiceLevelControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.paramFocusedReadout and self.shiftSnapshot then
      if self.paramFocusedReadout:getValueInUnits() ~= self.shiftSnapshot then
        self.shiftHeld, self.shiftSnapshot = false, nil; return true
      end
    end
    self:setParamMode(not self.paramMode)
  end
  self.shiftHeld, self.shiftSnapshot = false, nil; return true
end

function DrumVoiceLevelControl:subReleased(i, shifted)
  if self.paramMode then
    local rs = { self.clipperReadout, self.eqReadout }
    local ls = { "clipper", "eq" }
    local r = rs[i]
    if r then
      if shifted then ShiftHelpers.openKeyboardFor(r, ls[i])
      else r:save(); self.paramFocusedReadout = r; self:setSubCursorController(r)
        if not self:hasFocus("encoder") then self:focus() end
      end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function DrumVoiceLevelControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil; self:setSubCursorController(nil); self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function DrumVoiceLevelControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine); return true
  end
  return GainBias.encoder(self, change, shifted)
end

function DrumVoiceLevelControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then self.paramFocusedReadout:zero(); return true end
  return GainBias.zeroPressed(self)
end

function DrumVoiceLevelControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then self.paramFocusedReadout:restore(); return true end
  return GainBias.cancelReleased(self, shifted)
end

return DrumVoiceLevelControl
