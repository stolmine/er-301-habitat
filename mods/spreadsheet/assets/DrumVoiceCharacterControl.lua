local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1, col2, col3 = app.BUTTON1_CENTER, app.BUTTON2_CENTER, app.BUTTON3_CENTER

local DrumVoiceCharacterControl = Class {}
DrumVoiceCharacterControl:include(GainBias)

local function label(text, x)
  local g = app.Label(text, 10)
  g:fitToText(3); g:setSize(ply * 3, g.mHeight); g:setBorder(1)
  g:setCornerRadius(3, 0, 0, 3); g:setCenter(x, center1 + 1)
  return g
end

local function readout01(param, x)
  local m = app.LinearDialMap(0, 1); m:setSteps(0.1, 0.01, 0.001, 0.001)
  local g = app.Readout(0, 0, ply, 10)
  g:setParameter(param); g:setAttributes(app.unitNone, m); g:setPrecision(3)
  g:setCenter(x, center4); return g
end

function DrumVoiceCharacterControl:init(args)
  GainBias.init(self, args)

  local cube = libspreadsheet.DrumCubeGraphic(0, 0, ply, 64)
  cube:follow(args.op)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(cube)
  self:setMainCursorController(cube); self:setControlGraphic(container)

  self.shiftHeld, self.shiftUsed = false, false
  self.normalSubGraphic = self.subGraphic
  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)
  self.paramSubGraphic:addChild(label("Shape / Grit / Punch", col2))

  self.shapeReadout = readout01(args.shapeParam, col1)
  self.gritReadout  = readout01(args.gritParam,  col2)
  self.punchReadout = readout01(args.punchParam,  col3)
  self.paramModeDefaultSub = self.shapeReadout

  local sg = self.paramSubGraphic
  sg:addChild(self.shapeReadout); sg:addChild(self.gritReadout); sg:addChild(self.punchReadout)
  sg:addChild(app.SubButton("shape", 1)); sg:addChild(app.SubButton("grit", 2)); sg:addChild(app.SubButton("punch", 3))

  self:setParamMode(true)
end

function DrumVoiceCharacterControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode, self.paramFocusedReadout = enabled, nil
  self:setSubCursorController(nil)
  self.subGraphic = enabled and self.paramSubGraphic or self.normalSubGraphic
  if not enabled then self:setFocusedReadout(self.bias) end
  self:addSubGraphic(self.subGraphic)
end

function DrumVoiceCharacterControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then self:setSubCursorController(self.paramModeDefaultSub) end
end

function DrumVoiceCharacterControl:onCursorLeave(spot)
  if self.paramMode then self.paramFocusedReadout = nil; self:setSubCursorController(nil) end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function DrumVoiceCharacterControl:shiftPressed()
  self.shiftHeld, self.shiftUsed = true, false
  self.shiftSnapshot = self.paramFocusedReadout and self.paramFocusedReadout:getValueInUnits() or nil
  return true
end

function DrumVoiceCharacterControl:shiftReleased()
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

function DrumVoiceCharacterControl:subReleased(i, shifted)
  if self.paramMode then
    local rs = { self.shapeReadout, self.gritReadout, self.punchReadout }
    local ls = { "shape", "grit", "punch" }
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

function DrumVoiceCharacterControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil; self:setSubCursorController(nil); self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function DrumVoiceCharacterControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine); return true
  end
  return GainBias.encoder(self, change, shifted)
end

function DrumVoiceCharacterControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then self.paramFocusedReadout:zero(); return true end
  return GainBias.zeroPressed(self)
end

function DrumVoiceCharacterControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then self.paramFocusedReadout:restore(); return true end
  return GainBias.cancelReleased(self, shifted)
end

return DrumVoiceCharacterControl
