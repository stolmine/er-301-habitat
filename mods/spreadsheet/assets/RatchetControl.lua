local app = app
local Class = require "Base.Class"
local Base = require "Unit.ViewControl.EncoderControl"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local line1 = app.GRID5_LINE1
local line4 = app.GRID5_LINE4
local center1 = app.GRID5_CENTER1
local center3 = app.GRID5_CENTER3
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local multMap = (function()
  local m = app.LinearDialMap(1, 8)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

-- Gate mode flow diagram (same as TransformGateControl)
local gateInstructions = app.DrawingInstructions()
gateInstructions:box(col2 - 13, center3 - 8, 26, 16)
gateInstructions:startPolyline(col2 - 8, center3 - 4, 0)
gateInstructions:vertex(col2, center3 - 4)
gateInstructions:vertex(col2, center3 + 4)
gateInstructions:endPolyline(col2 + 8, center3 + 4)
gateInstructions:color(app.GRAY3)
gateInstructions:hline(col2 - 9, col2 + 9, center3)
gateInstructions:color(app.WHITE)
gateInstructions:circle(col3, center3, 8)
gateInstructions:hline(col1 + 20, col2 - 13, center3)
gateInstructions:triangle(col2 - 16, center3, 0, 3)
gateInstructions:hline(col2 + 13, col3 - 8, center3)
gateInstructions:triangle(col3 - 11, center3, 0, 3)
gateInstructions:vline(col3, center3 + 8, line1 - 2)
gateInstructions:triangle(col3, line1 - 2, 90, 3)
gateInstructions:vline(col3, line4, center3 - 8)
gateInstructions:triangle(col3, center3 - 11, 90, 3)

local RatchetControl = Class {
  type = "RatchetControl",
  canEdit = false,
  canMove = true
}
RatchetControl:include(Base)

function RatchetControl:init(args)
  local seq = args.seq or app.logError("%s.init: seq is missing.", self)
  local button = args.button or "ratch"
  local description = args.description or "Ratchet"
  local branch = args.branch or app.logError("%s.init: branch is missing.", self)
  local comparator = args.comparator or app.logError("%s.init: comparator is missing.", self)

  Base.init(self, button)
  self:setClassName("RatchetControl")

  self.seq = seq
  self.branch = branch
  self.comparator = comparator
  -- Decision 7 grandfather: default to ratchet params visible (paramMode=true=custom).
  self.paramMode = true
  self.shiftHeld = false
  self.shiftUsed = false

  -- Main graphic
  local graphic = app.ComparatorView(0, 0, ply, 64, comparator)
  graphic:setLabel(button)
  self.comparatorView = graphic
  self:setMainCursorController(graphic)
  self:setControlGraphic(graphic)
  self:addSpotDescriptor { center = 0.5 * ply }

  -- Two separate sub-graphics; swap via setParamMode (Decision 3).
  self.normalSubGraphic = app.Graphic(0, 0, 128, 64)
  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  ---- GATE (normal) MODE ELEMENTS ----
  self.gateDrawing = app.Drawing(0, 0, 128, 64)
  self.gateDrawing:add(gateInstructions)
  self.normalSubGraphic:addChild(self.gateDrawing)

  self.gateOrLabel = app.Label("or", 10)
  self.gateOrLabel:fitToText(0)
  self.gateOrLabel:setCenter(col3, center3 + 1)
  self.normalSubGraphic:addChild(self.gateOrLabel)

  self.gateScope = app.MiniScope(col1 - 20, line4, 40, 45)
  self.gateScope:setBorder(1)
  self.gateScope:setCornerRadius(3, 3, 3, 3)
  self.normalSubGraphic:addChild(self.gateScope)

  local threshParam = comparator:getParameter("Threshold")
  threshParam:enableSerialization()
  self.threshReadout = app.Readout(0, 0, ply, 10)
  self.threshReadout:setParameter(threshParam)
  self.threshReadout:setAttributes(app.unitNone, Encoder.getMap("default"))
  self.threshReadout:setCenter(col2, center4)
  self.normalSubGraphic:addChild(self.threshReadout)

  self.gateDesc = app.Label(description, 10)
  self.gateDesc:fitToText(3)
  self.gateDesc:setSize(ply * 2, self.gateDesc.mHeight)
  self.gateDesc:setBorder(1)
  self.gateDesc:setCornerRadius(3, 0, 0, 3)
  self.gateDesc:setCenter(0.5 * (col2 + col3), center1 + 1)
  self.normalSubGraphic:addChild(self.gateDesc)

  self.gateSub1 = app.SubButton("input", 1)
  self.gateSub2 = app.SubButton("thresh", 2)
  self.gateSub3 = app.SubButton("fire", 3)
  self.normalSubGraphic:addChild(self.gateSub1)
  self.normalSubGraphic:addChild(self.gateSub2)
  self.normalSubGraphic:addChild(self.gateSub3)

  ---- RATCHET (param) MODE ELEMENTS ----
  self.multReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.multParam
    if param then g:setParameter(param) end
    g:setAttributes(app.unitNone, multMap)
    g:setPrecision(0)
    g:setCenter(col1, center4)
    return g
  end)()

  self.lenLabel = app.Label("len:off", 10)
  self.lenLabel:fitToText(0)
  self.lenLabel:setCenter(col2, center3 + 1)
  self.paramSubGraphic:addChild(self.lenLabel)

  self.velLabel = app.Label("vel:off", 10)
  self.velLabel:fitToText(0)
  self.velLabel:setCenter(col3, center3 + 1)
  self.paramSubGraphic:addChild(self.velLabel)

  self.ratchetDesc = app.Label("Ratchet", 10)
  self.ratchetDesc:fitToText(3)
  self.ratchetDesc:setSize(ply * 2, self.ratchetDesc.mHeight)
  self.ratchetDesc:setBorder(1)
  self.ratchetDesc:setCornerRadius(3, 0, 0, 3)
  self.ratchetDesc:setCenter(0.5 * (col2 + col3), center1 + 1)

  self.ratchetSub1 = app.SubButton("mult", 1)
  self.ratchetSub2 = app.SubButton("len", 2)
  self.ratchetSub3 = app.SubButton("vel", 3)

  self.paramSubGraphic:addChild(self.multReadout)
  self.paramSubGraphic:addChild(self.ratchetDesc)
  self.paramSubGraphic:addChild(self.ratchetSub1)
  self.paramSubGraphic:addChild(self.ratchetSub2)
  self.paramSubGraphic:addChild(self.ratchetSub3)

  -- Store references for toggle
  self.lenToggleOption = args.lenToggleOption
  self.velToggleOption = args.velToggleOption
  if self.lenToggleOption then self.lenToggleOption:enableSerialization() end
  if self.velToggleOption then self.velToggleOption:enableSerialization() end

  -- Start in paramMode=true (ratchet params visible). EncoderControl.init
  -- built a default subGraphic; replace with paramSubGraphic for the
  -- grandfathered custom-default.
  self.subGraphic = self.paramSubGraphic

  branch:subscribe("contentChanged", self)
end

function RatchetControl:onRemove()
  self.branch:unsubscribe("contentChanged", self)
  Base.onRemove(self)
end

function RatchetControl:contentChanged(chain)
  if chain == self.branch then
    local outlet = chain:getMonitoringOutput(1)
    self.gateScope:watchOutlet(outlet)
    self.gateSub1:setText(chain:mnemonic())
  end
end

function RatchetControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  self.focusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.paramSubGraphic
  else
    self.subGraphic = self.normalSubGraphic
  end
  self:addSubGraphic(self.subGraphic)
end

function RatchetControl:updateToggleLabels()
  local lenOn = self.lenToggleOption and self.lenToggleOption:value() == 1
  local velOn = self.velToggleOption and self.velToggleOption:value() == 1
  self.lenLabel:setText(lenOn and "len:ON" or "len:off")
  self.velLabel:setText(velOn and "vel:ON" or "vel:off")
end

function RatchetControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function RatchetControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function RatchetControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function RatchetControl:onCursorEnter(spot)
  Base.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function RatchetControl:onCursorLeave(spot)
  -- Decision 7: paramMode persists across leave/return. Clear only the
  -- per-session focus so the user has to deliberately re-focus to edit.
  self.focusedReadout = nil
  self:setSubCursorController(nil)
  self:releaseFocus("shiftPressed", "shiftReleased")
  Base.onCursorLeave(self, spot)
end

function RatchetControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.focusedReadout then
    self.shiftSnapshot = self.focusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function RatchetControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.focusedReadout and self.shiftSnapshot then
      local cur = self.focusedReadout:getValueInUnits()
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

function RatchetControl:spotReleased(spot, shifted)
  -- Decision 2: no secondary toggle path on shift+spot. Just delegate.
  if Base.spotReleased(self, spot, shifted) then
    self:setFocusedReadout(nil)
    return true
  end
  return false
end

function RatchetControl:subPressed(i, shifted)
  if shifted then return false end
  if not self.paramMode then
    if i == 3 then
      self.comparator:simulateRisingEdge()
    end
  end
  return true
end

function RatchetControl:subReleased(i, shifted)
  if shifted then return false end

  if self.paramMode then
    if i == 1 then
      -- Mult readout
      if self:hasFocus("encoder") then
        self:setFocusedReadout(self.multReadout)
      else
        self:focus()
        self:setFocusedReadout(self.multReadout)
      end
    elseif i == 2 then
      -- Toggle len
      if self.lenToggleOption then
        local current = self.lenToggleOption:value()
        self.lenToggleOption:set(current == 1 and 2 or 1)
        self:updateToggleLabels()
      end
    elseif i == 3 then
      -- Toggle vel
      if self.velToggleOption then
        local current = self.velToggleOption:value()
        self.velToggleOption:set(current == 1 and 2 or 1)
        self:updateToggleLabels()
      end
    end
  else
    -- Gate mode
    if i == 1 then
      if self.branch then
        self:unfocus()
        self.branch:show()
      end
    elseif i == 2 then
      if self:hasFocus("encoder") then
        self:setFocusedReadout(self.threshReadout)
      else
        self:focus()
        self:setFocusedReadout(self.threshReadout)
      end
    elseif i == 3 then
      self.comparator:simulateFallingEdge()
    end
  end
  return true
end

function RatchetControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
  end
  return true
end

function RatchetControl:upReleased(shifted)
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

return RatchetControl
