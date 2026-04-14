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
  self.ratchetMode = true -- default to ratchet params

  -- Main graphic
  local graphic = app.ComparatorView(0, 0, ply, 64, comparator)
  graphic:setLabel(button)
  self.comparatorView = graphic
  self:setMainCursorController(graphic)
  self:setControlGraphic(graphic)
  self:addSpotDescriptor { center = 0.5 * ply }

  self.subGraphic = app.Graphic(0, 0, 128, 64)

  ---- GATE MODE ELEMENTS ----
  self.gateDrawing = app.Drawing(0, 0, 128, 64)
  self.gateDrawing:add(gateInstructions)
  self.subGraphic:addChild(self.gateDrawing)

  self.gateOrLabel = app.Label("or", 10)
  self.gateOrLabel:fitToText(0)
  self.gateOrLabel:setCenter(col3, center3 + 1)
  self.subGraphic:addChild(self.gateOrLabel)

  self.gateScope = app.MiniScope(col1 - 20, line4, 40, 45)
  self.gateScope:setBorder(1)
  self.gateScope:setCornerRadius(3, 3, 3, 3)
  self.subGraphic:addChild(self.gateScope)

  local threshParam = comparator:getParameter("Threshold")
  threshParam:enableSerialization()
  self.threshReadout = app.Readout(0, 0, ply, 10)
  self.threshReadout:setParameter(threshParam)
  self.threshReadout:setAttributes(app.unitNone, Encoder.getMap("default"))
  self.threshReadout:setCenter(col2, center4)
  self.subGraphic:addChild(self.threshReadout)

  self.gateDesc = app.Label(description, 10)
  self.gateDesc:fitToText(3)
  self.gateDesc:setSize(ply * 2, self.gateDesc.mHeight)
  self.gateDesc:setBorder(1)
  self.gateDesc:setCornerRadius(3, 0, 0, 3)
  self.gateDesc:setCenter(0.5 * (col2 + col3), center1 + 1)
  self.subGraphic:addChild(self.gateDesc)

  self.gateSub1 = app.SubButton("input", 1)
  self.gateSub2 = app.SubButton("thresh", 2)
  self.gateSub3 = app.SubButton("fire", 3)
  self.subGraphic:addChild(self.gateSub1)
  self.subGraphic:addChild(self.gateSub2)
  self.subGraphic:addChild(self.gateSub3)

  ---- RATCHET MODE ELEMENTS ----
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
  self.subGraphic:addChild(self.lenLabel)

  self.velLabel = app.Label("vel:off", 10)
  self.velLabel:fitToText(0)
  self.velLabel:setCenter(col3, center3 + 1)
  self.subGraphic:addChild(self.velLabel)

  self.ratchetDesc = app.Label("Ratchet", 10)
  self.ratchetDesc:fitToText(3)
  self.ratchetDesc:setSize(ply * 2, self.ratchetDesc.mHeight)
  self.ratchetDesc:setBorder(1)
  self.ratchetDesc:setCornerRadius(3, 0, 0, 3)
  self.ratchetDesc:setCenter(0.5 * (col2 + col3), center1 + 1)

  self.ratchetSub1 = app.SubButton("mult", 1)
  self.ratchetSub2 = app.SubButton("len", 2)
  self.ratchetSub3 = app.SubButton("vel", 3)

  self.subGraphic:addChild(self.multReadout)
  self.subGraphic:addChild(self.ratchetDesc)
  self.subGraphic:addChild(self.ratchetSub1)
  self.subGraphic:addChild(self.ratchetSub2)
  self.subGraphic:addChild(self.ratchetSub3)

  -- Store references for toggle
  self.lenToggleOption = args.lenToggleOption
  self.velToggleOption = args.velToggleOption
  if self.lenToggleOption then self.lenToggleOption:enableSerialization() end
  if self.velToggleOption then self.velToggleOption:enableSerialization() end

  self:setRatchetMode(true)
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

function RatchetControl:setRatchetMode(enabled)
  self.ratchetMode = enabled

  -- Gate elements
  if enabled then
    self.gateDrawing:hide()
    self.gateOrLabel:hide()
    self.gateScope:hide()
    self.threshReadout:hide()
    self.gateDesc:hide()
    self.gateSub1:hide()
    self.gateSub2:hide()
    self.gateSub3:hide()
  else
    self.gateDrawing:show()
    self.gateOrLabel:show()
    self.gateScope:show()
    self.threshReadout:show()
    self.gateDesc:show()
    self.gateSub1:show()
    self.gateSub2:show()
    self.gateSub3:show()
  end

  -- Ratchet elements
  if enabled then
    self.multReadout:show()
    self.lenLabel:show()
    self.velLabel:show()
    self.ratchetDesc:show()
    self.ratchetSub1:show()
    self.ratchetSub2:show()
    self.ratchetSub3:show()
  else
    self.multReadout:hide()
    self.lenLabel:hide()
    self.velLabel:hide()
    self.ratchetDesc:hide()
    self.ratchetSub1:hide()
    self.ratchetSub2:hide()
    self.ratchetSub3:hide()
  end

  self.focusedReadout = nil
  self:setSubCursorController(nil)
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
  self:releaseFocus("shiftPressed", "shiftReleased")
  Base.onCursorLeave(self, spot)
end

function RatchetControl:shiftPressed()
  if self.focusedReadout then
    self.shiftDeferred = true
    self.shiftSnapshot = self.focusedReadout:getValueInUnits()
    return true
  end
  self.shiftDeferred = false
  self:setRatchetMode(not self.ratchetMode)
  return true
end

function RatchetControl:shiftReleased()
  if self.shiftDeferred then
    self.shiftDeferred = false
    if self.focusedReadout and self.shiftSnapshot then
      local cur = self.focusedReadout:getValueInUnits()
      if cur == self.shiftSnapshot then
        self:setRatchetMode(not self.ratchetMode)
      end
    else
      self:setRatchetMode(not self.ratchetMode)
    end
    self.shiftSnapshot = nil
  end
  return true
end

function RatchetControl:spotReleased(spot, shifted)
  if shifted then
    self:setRatchetMode(not self.ratchetMode)
    return true
  end
  if Base.spotReleased(self, spot, shifted) then
    self:setFocusedReadout(nil)
    return true
  end
  return false
end

function RatchetControl:subPressed(i, shifted)
  if shifted then return false end
  if not self.ratchetMode then
    if i == 3 then
      self.comparator:simulateRisingEdge()
    end
  end
  return true
end

function RatchetControl:subReleased(i, shifted)
  if shifted then return false end

  if self.ratchetMode then
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
