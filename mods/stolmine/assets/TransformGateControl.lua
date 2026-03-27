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
  self.mathMode = false

  -- Main graphic: simple label
  local graphic = app.Graphic(0, 0, ply, 64)
  local label = app.Label("xform", 10)
  label:setCenter(ply / 2, 32)
  graphic:addChild(label)
  self:setMainCursorController(graphic)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * ply }

  -- Gate sub-display
  self.gateSubGraphic = app.Graphic(0, 0, 128, 64)
  local gateDesc = app.Label("Transform Gate", 10)
  gateDesc:fitToText(3)
  gateDesc:setSize(ply * 3, gateDesc.mHeight)
  gateDesc:setBorder(1)
  gateDesc:setCornerRadius(3, 0, 0, 3)
  gateDesc:setCenter(col2, center1 + 1)
  self.gateSubGraphic:addChild(gateDesc)
  self.gateSubGraphic:addChild(app.SubButton("shift:math", 1))

  -- Math sub-display
  self.mathSubGraphic = app.Graphic(0, 0, 128, 64)

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

  self.mathDesc = (function()
    local g = app.Label("Transform", 10)
    g:fitToText(3)
    g:setSize(ply * 3, g.mHeight)
    g:setBorder(1)
    g:setCornerRadius(3, 0, 0, 3)
    g:setCenter(col2, center1 + 1)
    return g
  end)()

  self.mathSubGraphic:addChild(self.funcReadout)
  self.mathSubGraphic:addChild(self.factorReadout)
  self.mathSubGraphic:addChild(self.mathDesc)
  self.mathSubGraphic:addChild(app.SubButton("func", 1))
  self.mathSubGraphic:addChild(app.SubButton("factor", 2))
  self.mathSubGraphic:addChild(app.SubButton("fire!", 3))

  -- Start in gate mode
  self.subGraphic = self.gateSubGraphic
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
    -- Toggle math/gate sub-display
    self.mathMode = not self.mathMode
    self.subGraphic = self.mathMode and self.mathSubGraphic or self.gateSubGraphic
    self.focusedReadout = nil
    self:setSubCursorController(nil)
    return true
  end
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
      -- Fire!
      self.seq:fireTransform()
    end
  else
    if i == 1 then
      -- Toggle to math mode
      self.mathMode = true
      self.subGraphic = self.mathSubGraphic
      self.focusedReadout = nil
      self:setSubCursorController(nil)
    end
  end
  return true
end

function TransformGateControl:encoder(change, shifted)
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Coarse)
    -- Update func label
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
