-- MixInputControl -- custom ViewControl for Blanda input plies.
--
-- Extends GainBias so the main-view encoder edits Level by default.
-- Main view is a MiniScope backdrop overlaid with a BlandaInputGraphic
-- (bell landscape + peak tick + rising level indicator at scan).
--
-- Dual-mode sub-display (shift toggles between them):
--   default: stock-style mix-input sub (MiniScope sub-buttons: input /
--     Solo / Mute, with BinaryIndicators). Follows the BranchMeter pattern
--     so it plugs into the Unit's mute group.
--   shifted: our sub-params (Level / Weight / Offset) as three readouts
--     with three sub-buttons that focus the encoder on each.

local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center3 = app.GRID5_CENTER3
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local MixInputControl = Class {
  type = "MixInputControl",
  canEdit = false,
  canMove = true
}
MixInputControl:include(GainBias)

function MixInputControl:init(args)
  GainBias.init(self, args)

  self:setClassName("MixInputControl")

  self.branch = args.branch or app.logError("%s.init: branch missing", self)
  self.inputIndex = args.inputIndex or 0

  -- ---- Main view ----
  local container = app.Graphic(0, 0, ply, 64)

  self.mainScope = app.MiniScope(0, 0, ply, 64)
  self.mainScope:setBorder(1)
  self.mainScope:setCornerRadius(3, 3, 3, 3)
  container:addChild(self.mainScope)

  self.landscape = libspreadsheet.BlandaInputGraphic(0, 0, ply, 64)
  self.landscape:setIndex(self.inputIndex)
  if args.op then self.landscape:follow(args.op) end
  container:addChild(self.landscape)

  self:setMainCursorController(container)
  self:setControlGraphic(container)
  self:addSpotDescriptor { center = 0.5 * ply }

  -- ---- Sub-display: build both modes, default = mixSubGraphic ----
  -- GainBias built a default subGraphic already; drop it.
  self:removeSubGraphic(self.subGraphic)

  -- Default mode: BranchMeter-style scope + input/solo/mute.
  self.mixSubGraphic = app.Graphic(0, 0, 128, 64)

  self.scope = app.MiniScope(0, 15, ply, 64 - 15)
  self.scope:setBorder(1)
  self.scope:setCornerRadius(3, 3, 3, 3)
  self.mixSubGraphic:addChild(self.scope)

  self.inputSubButton = app.SubButton("", 1)
  self.mixSubGraphic:addChild(self.inputSubButton)

  self.soloSubButton = app.TextPanel("Solo", 2, 0.25)
  self.soloIndicator = app.BinaryIndicator(0, 24, ply, 32)
  self.soloSubButton:setLeftBorder(0)
  self.soloSubButton:addChild(self.soloIndicator)
  self.mixSubGraphic:addChild(self.soloSubButton)

  self.muteSubButton = app.TextPanel("Mute", 3, 0.25)
  self.muteIndicator = app.BinaryIndicator(0, 24, ply, 32)
  self.muteSubButton:addChild(self.muteIndicator)
  self.mixSubGraphic:addChild(self.muteSubButton)

  -- Shift mode: Level / Weight / Offset readouts + sub-buttons.
  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label(args.description or ("Input " .. (self.inputIndex + 1)), 10)
  desc:fitToText(3)
  desc:setSize(ply * 2, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(0.5 * (col2 + col3), center1 + 1)
  self.paramSubGraphic:addChild(desc)

  local function makeReadout(param, map, precision, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(args.biasUnits or app.unitNone, map)
    g:setPrecision(precision)
    g:setCenter(x, center4)
    return g
  end

  local levelMap = args.biasMap or app.LinearDialMap(0, 2)
  local weightMap = args.weightMap or app.LinearDialMap(0, 2)
  local offsetMap = args.offsetMap or app.LinearDialMap(0, 1)

  local levelParam = args.levelParam or args.gainbias:getParameter("Bias")
  self.levelReadout  = makeReadout(levelParam, levelMap, args.biasPrecision or 2, col1)
  self.weightReadout = makeReadout(args.weightParam, weightMap, 2, col2)
  self.offsetReadout = makeReadout(args.offsetParam, offsetMap, 2, col3)

  self.paramSubGraphic:addChild(self.levelReadout)
  self.paramSubGraphic:addChild(self.weightReadout)
  self.paramSubGraphic:addChild(self.offsetReadout)
  self.paramSubGraphic:addChild(app.SubButton("level", 1))
  self.paramSubGraphic:addChild(app.SubButton("wght", 2))
  self.paramSubGraphic:addChild(app.SubButton("ofst", 3))

  -- Start in mix mode. In this mode the encoder edits Level via the
  -- GainBias default (self.bias is the Level readout), so point
  -- focusedReadout there for onFocused/zero/cancel routing.
  self.mixMode = true
  self.shiftHeld = false
  self.shiftUsed = false
  self.subGraphic = self.mixSubGraphic
  self:addSubGraphic(self.subGraphic)
  self.focusedReadout = self.bias

  self.branch:subscribe("contentChanged", self)
end

function MixInputControl:onRemove()
  self.branch:unsubscribe("contentChanged", self)
  GainBias.onRemove(self)
end

function MixInputControl:contentChanged(chain)
  if chain ~= self.branch then return end
  local outlet = chain:getMonitoringOutput(1)
  if outlet then
    if self.mainScope then self.mainScope:watchOutlet(outlet) end
    if self.scope then self.scope:watchOutlet(outlet) end
  end
  self.inputSubButton:setText(chain:mnemonic())
end

-- Mute-group hooks (same interface BranchMeter exposes) -----------------

function MixInputControl:mute()
  self:callUp("muteControl", self)
end

function MixInputControl:solo()
  self:callUp("soloControl", self)
end

function MixInputControl:isMuted()
  return self.muteIndicator:value()
end

function MixInputControl:isSolo()
  return self.soloIndicator:value()
end

function MixInputControl:onMuteStateChanged(muted, solo)
  local branch = self.branch
  if muted then
    self.muteIndicator:on()
    if solo == self then
      self.soloIndicator:on()
      branch:unmute()
    else
      self.soloIndicator:off()
      branch:mute()
    end
  else
    self.muteIndicator:off()
    if solo == self then
      self.soloIndicator:on()
      branch:unmute()
    elseif solo then
      self.soloIndicator:off()
      branch:mute()
    else
      self.soloIndicator:off()
      branch:unmute()
    end
  end
end

-- Sub-mode toggle --------------------------------------------------------

function MixInputControl:setMixMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.mixMode = enabled
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.mixSubGraphic
    -- Keep Level editable in mix mode via GainBias default encoder path.
    self.focusedReadout = self.bias
  else
    self.subGraphic = self.paramSubGraphic
    -- In param mode we route via paramFocusedReadout; nothing "default."
    self.focusedReadout = nil
  end
  self:addSubGraphic(self.subGraphic)
end

function MixInputControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function MixInputControl:onCursorLeave(spot)
  if not self.mixMode then
    -- Reset to mix mode when leaving so other plies see a fresh default.
    self:removeSubGraphic(self.subGraphic)
    self.mixMode = true
    self.subGraphic = self.mixSubGraphic
    self:addSubGraphic(self.subGraphic)
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function MixInputControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function MixInputControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.paramFocusedReadout and self.shiftSnapshot then
      local cur = self.paramFocusedReadout:getValueInUnits()
      if cur ~= self.shiftSnapshot then
        self.shiftHeld = false
        self.shiftSnapshot = nil
        return true
      end
    end
    self:setMixMode(not self.mixMode)
  end
  self.shiftHeld = false
  self.shiftSnapshot = nil
  return true
end

-- Readout focus helpers --------------------------------------------------

function MixInputControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.paramFocusedReadout = readout
  self:setSubCursorController(readout)
end

-- Sub-button dispatch depending on mode ----------------------------------

function MixInputControl:subReleased(i, shifted)
  if shifted then return false end
  if self.mixMode then
    if i == 1 then
      self:unfocus()
      self.branch:show()
    elseif i == 2 then
      self:callUp("toggleSoloOnControl", self)
    elseif i == 3 then
      self:callUp("toggleMuteOnControl", self)
    end
    return true
  else
    local r = (i == 1) and self.levelReadout
          or  (i == 2) and self.weightReadout
          or  (i == 3) and self.offsetReadout
    if r then
      if not self:hasFocus("encoder") then self:focus() end
      self:setFocusedReadout(r)
      return true
    end
  end
  return false
end

function MixInputControl:spotReleased(spot, shifted)
  if not self.mixMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setMixMode(true)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

-- Encoder routing --------------------------------------------------------

function MixInputControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function MixInputControl:zeroPressed()
  if self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function MixInputControl:cancelReleased(shifted)
  if self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

function MixInputControl:upReleased(shifted)
  if self.paramFocusedReadout then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    return true
  end
  return GainBias.upReleased(self, shifted)
end

return MixInputControl
