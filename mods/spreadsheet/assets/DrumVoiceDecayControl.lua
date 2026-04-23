local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1, col2 = app.BUTTON1_CENTER, app.BUTTON2_CENTER

local DrumVoiceDecayControl = Class {}
DrumVoiceDecayControl:include(GainBias)

local function label(text, x)
  local g = app.Label(text, 10)
  g:fitToText(3); g:setSize(ply * 3, g.mHeight); g:setBorder(1)
  g:setCornerRadius(3, 0, 0, 3); g:setCenter(x, center1 + 1)
  return g
end

local function makeReadout(param, map, x)
  local g = app.Readout(0, 0, ply, 10)
  g:setParameter(param); g:setAttributes(app.unitSecs, map); g:setPrecision(3)
  g:setCenter(x, center4); return g
end

function DrumVoiceDecayControl:init(args)
  GainBias.init(self, args)

  self.paramMode, self.shiftHeld, self.shiftUsed = false, false, false
  self.normalSubGraphic = self.subGraphic
  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)
  self.paramSubGraphic:addChild(label("Hold / Attack", col2))

  local holdMap = app.LinearDialMap(0, 0.5); holdMap:setSteps(0.05, 0.01, 0.001, 0.001)
  local attackMap = app.LinearDialMap(0, 0.05); attackMap:setSteps(0.005, 0.001, 0.001, 0.001)

  self.holdReadout   = makeReadout(args.holdParam,   holdMap,   col1)
  self.attackReadout = makeReadout(args.attackParam,  attackMap, col2)

  local sg = self.paramSubGraphic
  sg:addChild(self.holdReadout); sg:addChild(self.attackReadout)
  sg:addChild(app.SubButton("hold", 1)); sg:addChild(app.SubButton("atk", 2))
end

function DrumVoiceDecayControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode, self.paramFocusedReadout = enabled, nil
  self:setSubCursorController(nil)
  self.subGraphic = enabled and self.paramSubGraphic or self.normalSubGraphic
  if not enabled then self:setFocusedReadout(self.bias) end
  self:addSubGraphic(self.subGraphic)
end

function DrumVoiceDecayControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then self:setSubCursorController(self.paramModeDefaultSub) end
end

function DrumVoiceDecayControl:onCursorLeave(spot)
  if self.paramMode then self.paramFocusedReadout = nil; self:setSubCursorController(nil) end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function DrumVoiceDecayControl:shiftPressed()
  self.shiftHeld, self.shiftUsed = true, false
  self.shiftSnapshot = self.paramFocusedReadout and self.paramFocusedReadout:getValueInUnits() or nil
  return true
end

function DrumVoiceDecayControl:shiftReleased()
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

function DrumVoiceDecayControl:subReleased(i, shifted)
  if self.paramMode then
    local rs = { self.holdReadout, self.attackReadout }
    local ls = { "hold", "attack" }
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

function DrumVoiceDecayControl:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil; self:setSubCursorController(nil); self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function DrumVoiceDecayControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine); return true
  end
  return GainBias.encoder(self, change, shifted)
end

function DrumVoiceDecayControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then self.paramFocusedReadout:zero(); return true end
  return GainBias.zeroPressed(self)
end

function DrumVoiceDecayControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then self.paramFocusedReadout:restore(); return true end
  return GainBias.cancelReleased(self, shifted)
end

return DrumVoiceDecayControl
