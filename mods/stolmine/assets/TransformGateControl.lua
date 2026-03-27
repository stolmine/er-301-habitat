local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Base = require "Unit.ViewControl.EncoderControl"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY
local center1 = app.GRID5_CENTER1
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

local funcNames = {
  [0] = "add", "sub", "mul", "div", "mod", "rev", "rot", "inv", "rnd"
}

local funcMap = (function()
  local m = app.LinearDialMap(0, 8)
  m:setSteps(1, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local factorMap = (function()
  local m = app.LinearDialMap(1, 64)
  m:setSteps(4, 1, 1, 1)
  m:setRounding(1)
  return m
end)()

local TransformGateControl = Class {
  type = "TransformGateControl",
  canEdit = false,
  canMove = true
}
TransformGateControl:include(Base)

function TransformGateControl:init(args)
  local seq = args.seq or app.logError("%s.init: seq is missing.", self)

  Base.init(self, "xform")
  self:setClassName("TransformGateControl")

  self.seq = seq
  self.comparator = args.comparator
  self.mathMode = false

  -- Main graphic
  local graphic = app.Graphic(0, 0, ply, 64)
  local label = app.Label("xform", 10)
  label:setCenter(ply / 2, 32)
  graphic:addChild(label)
  self:setMainCursorController(graphic)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * ply }

  -- Single sub-graphic with both modes as show/hide groups
  self.subGraphic = app.Graphic(0, 0, 128, 64)

  -- Gate mode elements
  local line4 = app.GRID5_LINE4
  local center3 = app.GRID5_CENTER3

  self.gateScope = app.MiniScope(col1 - 20, line4, 40, 45)
  self.gateScope:setBorder(1)
  self.gateScope:setCornerRadius(3, 3, 3, 3)
  if args.branch then
    self.gateScope:watchOutlet(args.branch:getMonitoringOutput(1))
  end
  self.subGraphic:addChild(self.gateScope)

  local threshParam = args.comparator:getParameter("Threshold")
  threshParam:enableSerialization()
  self.threshReadout = app.Readout(0, 0, ply, 10)
  self.threshReadout:setParameter(threshParam)
  self.threshReadout:setAttributes(app.unitNone, Encoder.getMap("default"))
  self.threshReadout:setCenter(col2, center4)
  self.subGraphic:addChild(self.threshReadout)

  self.gateDesc = app.Label("Xform Gate", 10)
  self.gateDesc:fitToText(3)
  self.gateDesc:setSize(ply * 2, self.gateDesc.mHeight)
  self.gateDesc:setBorder(1)
  self.gateDesc:setCornerRadius(3, 0, 0, 3)
  self.gateDesc:setCenter(0.5 * (col2 + col3), center1 + 1)
  self.subGraphic:addChild(self.gateDesc)

  self.gateOrLabel = app.Label("or", 10)
  self.gateOrLabel:fitToText(0)
  self.gateOrLabel:setCenter(col3, center3 + 1)
  self.subGraphic:addChild(self.gateOrLabel)

  self.gateSub1 = app.SubButton("input", 1)
  self.gateSub2 = app.SubButton("thresh", 2)
  self.gateSub3 = app.SubButton("fire", 3)
  self.subGraphic:addChild(self.gateSub1)
  self.subGraphic:addChild(self.gateSub2)
  self.subGraphic:addChild(self.gateSub3)

  -- Math mode elements
  self.funcReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.funcParam
    if param then g:setParameter(param) end
    g:setAttributes(app.unitNone, funcMap)
    g:setPrecision(0)
    g:setCenter(col1, center4)
    return g
  end)()

  self.factorReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    local param = args.factorParam
    if param then g:setParameter(param) end
    g:setAttributes(app.unitNone, factorMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    return g
  end)()

  self.mathDesc = app.Label("Transform", 10)
  self.mathDesc:fitToText(3)
  self.mathDesc:setSize(ply * 3, self.mathDesc.mHeight)
  self.mathDesc:setBorder(1)
  self.mathDesc:setCornerRadius(3, 0, 0, 3)
  self.mathDesc:setCenter(col2, center1 + 1)

  self.mathSub1 = app.SubButton("func", 1)
  self.mathSub2 = app.SubButton("factor", 2)
  self.mathSub3 = app.SubButton("fire!", 3)

  self.subGraphic:addChild(self.funcReadout)
  self.subGraphic:addChild(self.factorReadout)
  self.subGraphic:addChild(self.mathDesc)
  self.subGraphic:addChild(self.mathSub1)
  self.subGraphic:addChild(self.mathSub2)
  self.subGraphic:addChild(self.mathSub3)

  -- Start in gate mode
  self:setMathMode(false)
end

function TransformGateControl:setMathMode(enabled)
  self.mathMode = enabled

  -- Gate elements
  if enabled then
    self.gateScope:hide()
    self.threshReadout:hide()
    self.gateDesc:hide()
    self.gateOrLabel:hide()
    self.gateSub1:hide()
    self.gateSub2:hide()
    self.gateSub3:hide()
  else
    self.gateScope:show()
    self.threshReadout:show()
    self.gateDesc:show()
    self.gateOrLabel:show()
    self.gateSub1:show()
    self.gateSub2:show()
    self.gateSub3:show()
  end

  -- Math elements
  if enabled then
    self.funcReadout:show()
    self.factorReadout:show()
    self.mathDesc:show()
    self.mathSub1:show()
    self.mathSub2:show()
    self.mathSub3:show()
  else
    self.funcReadout:hide()
    self.factorReadout:hide()
    self.mathDesc:hide()
    self.mathSub1:hide()
    self.mathSub2:hide()
    self.mathSub3:hide()
  end

  self.focusedReadout = nil
  self:setSubCursorController(nil)
end

function TransformGateControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function TransformGateControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function TransformGateControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function TransformGateControl:spotReleased(spot, shifted)
  if shifted then
    self:setMathMode(not self.mathMode)
    return true
  end
  return true
end

function TransformGateControl:onCursorEnter(spot)
  Base.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function TransformGateControl:onCursorLeave(spot)
  self:releaseFocus("shiftPressed", "shiftReleased")
  Base.onCursorLeave(self, spot)
end

function TransformGateControl:shiftPressed()
  self:setMathMode(not self.mathMode)
  return true
end

function TransformGateControl:shiftReleased()
  return true
end

function TransformGateControl:subReleased(i, shifted)
  if shifted then return false end

  if self.mathMode then
    if i == 1 then
      if self:hasFocus("encoder") then
        self:setFocusedReadout(self.funcReadout)
      else
        self:focus()
        self:setFocusedReadout(self.funcReadout)
      end
    elseif i == 2 then
      if self:hasFocus("encoder") then
        self:setFocusedReadout(self.factorReadout)
      else
        self:focus()
        self:setFocusedReadout(self.factorReadout)
      end
    elseif i == 3 then
      self.seq:fireTransform()
    end
  else
    if i == 2 then
      -- Threshold readout
      if self:hasFocus("encoder") then
        self:setFocusedReadout(self.threshReadout)
      else
        self:focus()
        self:setFocusedReadout(self.threshReadout)
      end
    elseif i == 3 then
      -- Fire gate manually
      self.comparator:simulateRisingEdge()
      self.comparator:simulateFallingEdge()
    end
  end
  return true
end

function TransformGateControl:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Coarse)
    if self.focusedReadout == self.funcReadout then
      local val = math.floor(self.funcReadout:getValueInUnits() + 0.5)
      local name = funcNames[val]
      if name then
        self.mathDesc:setText("xf: " .. name)
      end
    end
  end
  return true
end

function TransformGateControl:upReleased(shifted)
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

return TransformGateControl
