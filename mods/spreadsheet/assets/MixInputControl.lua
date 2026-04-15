-- MixInputControl -- custom ViewControl for Blanda input plies.
--
-- Extends GainBias so the main-view encoder edits Level by default. The
-- default fader is replaced by a container of { MiniScope, BlandaInputGraphic }.
-- The sub-display is rebuilt with three readouts (Level / Weight / Offset)
-- plus three sub-buttons:
--   sub1 "input" -- opens the branch so the user can patch a chain in
--   sub2 "wght"  -- focus encoder on the Weight readout
--   sub3 "ofst"  -- focus encoder on the Offset readout
-- No shift-toggle -- the sub-display is static.

local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
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
  -- GainBias.init binds the encoder to args.gainbias.
  GainBias.init(self, args)

  self:setClassName("MixInputControl")

  self.branch = args.branch or app.logError("%s.init: branch missing", self)
  self.inputIndex = args.inputIndex or 0

  -- ---- Main view: MiniScope backdrop + BlandaInputGraphic overlay ----
  local container = app.Graphic(0, 0, ply, 64)

  self.scope = app.MiniScope(0, 0, ply, 64)
  self.scope:setBorder(1)
  self.scope:setCornerRadius(3, 3, 3, 3)
  container:addChild(self.scope)

  self.landscape = libspreadsheet.BlandaInputGraphic(0, 0, ply, 64)
  self.landscape:setIndex(self.inputIndex)
  if args.op then self.landscape:follow(args.op) end
  container:addChild(self.landscape)

  self:setMainCursorController(container)
  self:setControlGraphic(container)
  self:addSpotDescriptor { center = 0.5 * ply }

  -- ---- Sub-display: Level / Weight / Offset readouts + sub-buttons ----
  -- Replace GainBias's default sub entirely.
  self.defaultSubGraphic = self.subGraphic
  self:removeSubGraphic(self.subGraphic)

  local subGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label(args.description or ("Input " .. (self.inputIndex + 1)), 10)
  desc:fitToText(3)
  desc:setSize(ply * 2, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(0.5 * (col2 + col3), center1 + 1)
  subGraphic:addChild(desc)

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

  self.levelReadout  = makeReadout(self.bias, levelMap, args.biasPrecision or 2, col1)
  self.weightReadout = makeReadout(args.weightParam, weightMap, 2, col2)
  self.offsetReadout = makeReadout(args.offsetParam, offsetMap, 2, col3)

  subGraphic:addChild(self.levelReadout)
  subGraphic:addChild(self.weightReadout)
  subGraphic:addChild(self.offsetReadout)
  subGraphic:addChild(app.SubButton("input", 1))
  subGraphic:addChild(app.SubButton("wght", 2))
  subGraphic:addChild(app.SubButton("ofst", 3))

  self.subGraphic = subGraphic
  self:addSubGraphic(self.subGraphic)

  -- Watch the branch output so the MiniScope shows whatever chain is patched.
  self.branch:subscribe("contentChanged", self)
end

function MixInputControl:onRemove()
  self.branch:unsubscribe("contentChanged", self)
  GainBias.onRemove(self)
end

function MixInputControl:contentChanged(chain)
  if chain == self.branch then
    local outlet = chain:getMonitoringOutput(1)
    if outlet and self.scope then self.scope:watchOutlet(outlet) end
  end
end

function MixInputControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function MixInputControl:subReleased(i, shifted)
  if shifted then return false end
  if i == 1 then
    -- Open the branch for chain patching.
    self:unfocus()
    self.branch:show()
    return true
  elseif i == 2 then
    if not self:hasFocus("encoder") then self:focus() end
    self:setFocusedReadout(self.weightReadout)
    return true
  elseif i == 3 then
    if not self:hasFocus("encoder") then self:focus() end
    self:setFocusedReadout(self.offsetReadout)
    return true
  end
  return false
end

function MixInputControl:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  -- Fall back to default GainBias main-view encoder (edits Level via bias).
  return GainBias.encoder(self, change, shifted)
end

function MixInputControl:zeroPressed()
  if self.focusedReadout then
    self.focusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function MixInputControl:cancelReleased(shifted)
  if self.focusedReadout then
    self.focusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

function MixInputControl:upReleased(shifted)
  if self.focusedReadout then
    self.focusedReadout = nil
    self:setSubCursorController(nil)
    return true
  end
  return GainBias.upReleased(self, shifted)
end

return MixInputControl
