local app = app
local libstolmine = require "spreadsheet.libspreadsheet"
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

local defaultFuncNames = {
  [0] = "add", "sub", "mul", "div", "mod", "rev", "rot", "inv", "rnd"
}

local defaultFuncMap = (function()
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

-- Gate mode flow diagram (matches SDK Gate pattern)
local gateInstructions = app.DrawingInstructions()
-- threshold box with waveform
gateInstructions:box(col2 - 13, center3 - 8, 26, 16)
gateInstructions:startPolyline(col2 - 8, center3 - 4, 0)
gateInstructions:vertex(col2, center3 - 4)
gateInstructions:vertex(col2, center3 + 4)
gateInstructions:endPolyline(col2 + 8, center3 + 4)
gateInstructions:color(app.GRAY3)
gateInstructions:hline(col2 - 9, col2 + 9, center3)
gateInstructions:color(app.WHITE)
-- or circle
gateInstructions:circle(col3, center3, 8)
-- arrow: branch to thresh
gateInstructions:hline(col1 + 20, col2 - 13, center3)
gateInstructions:triangle(col2 - 16, center3, 0, 3)
-- arrow: thresh to or
gateInstructions:hline(col2 + 13, col3 - 8, center3)
gateInstructions:triangle(col3 - 11, center3, 0, 3)
-- arrow: or to title
gateInstructions:vline(col3, center3 + 8, line1 - 2)
gateInstructions:triangle(col3, line1 - 2, 90, 3)
-- arrow: fire to or
gateInstructions:vline(col3, line4, center3 - 8)
gateInstructions:triangle(col3, center3 - 11, 90, 3)

-- Math mode fire graphic (col3: fire -> arrow -> circle -> arrow -> title)
local mathInstructions = app.DrawingInstructions()
-- circle at col3
mathInstructions:circle(col3, center3, 8)
-- arrow: fire to circle
mathInstructions:vline(col3, line4, center3 - 8)
mathInstructions:triangle(col3, center3 - 11, 90, 3)
-- arrow: circle to title
mathInstructions:vline(col3, center3 + 8, line1 - 2)
mathInstructions:triangle(col3, line1 - 2, 90, 3)

local scopeNames = { [0] = "ofst", "len", "dev", "all" }

local TransformGateControl = Class {
  type = "TransformGateControl",
  canEdit = false,
  canMove = true
}
TransformGateControl:include(Base)

function TransformGateControl:init(args)
  local seq = args.seq or app.logError("%s.init: seq is missing.", self)
  local button = args.button or "xform"
  local description = args.description or "Transform"
  local branch = args.branch or app.logError("%s.init: branch is missing.", self)
  local comparator = args.comparator or app.logError("%s.init: comparator is missing.", self)

  Base.init(self, button)
  self:setClassName("TransformGateControl")

  self.seq = seq
  self.branch = branch
  self.comparator = comparator
  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false
  self.funcNames = args.funcNames or defaultFuncNames
  local funcMap = args.funcMap or defaultFuncMap

  -- Main graphic: ComparatorView (same as Gate)
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

  ---- MATH (param) MODE ELEMENTS ----

  self.mathDrawing = app.Drawing(0, 0, 128, 64)
  self.mathDrawing:add(mathInstructions)

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
    g:setAttributes(app.unitNone, args.factorMap or factorMap)
    g:setPrecision(args.factorPrecision or 0)
    g:setCenter(col2, center4)
    return g
  end)()

  self.funcLabel = app.Label(self.funcNames[0] or "add", 10)
  self.funcLabel:fitToText(0)
  self.funcLabel:setCenter(col1, center3 + 1)

  self.mathDesc = app.Label("Transform", 10)
  self.mathDesc:fitToText(3)
  self.mathDesc:setSize(ply * 2, self.mathDesc.mHeight)
  self.mathDesc:setBorder(1)
  self.mathDesc:setCornerRadius(3, 0, 0, 3)
  self.mathDesc:setCenter(0.5 * (col2 + col3), center1 + 1)

  self.hasParamB = args.paramBParam ~= nil
  if self.hasParamB then
    self.paramBReadout = (function()
      local g = app.Readout(0, 0, ply, 10)
      g:setParameter(args.paramBParam)
      g:setAttributes(app.unitNone, args.paramBMap or factorMap)
      g:setPrecision(args.paramBPrecision or 0)
      g:setCenter(col3, center4)
      return g
    end)()
  end

  self.mathSub1 = app.SubButton("func", 1)
  self.mathSub2 = app.SubButton(args.paramALabel or "factor", 2)
  self.mathSub3 = app.SubButton(self.hasParamB and (args.paramBLabel or "prm B") or "fire!", 3)

  if not self.hasParamB then
    self.paramSubGraphic:addChild(self.mathDrawing)
  end
  self.paramSubGraphic:addChild(self.funcReadout)
  self.paramSubGraphic:addChild(self.factorReadout)
  if self.hasParamB then
    self.paramSubGraphic:addChild(self.paramBReadout)
  end
  self.paramSubGraphic:addChild(self.funcLabel)
  self.paramSubGraphic:addChild(self.mathDesc)
  self.paramSubGraphic:addChild(self.mathSub1)
  self.paramSubGraphic:addChild(self.mathSub2)
  self.paramSubGraphic:addChild(self.mathSub3)

  -- Start in normal (gate) mode. Decision 7: default preserved.
  -- EncoderControl.init built a default subGraphic; replace with normalSubGraphic.
  self.subGraphic = self.normalSubGraphic

  -- Subscribe to branch changes for scope updates
  branch:subscribe("contentChanged", self)
end

function TransformGateControl:onRemove()
  self.branch:unsubscribe("contentChanged", self)
  Base.onRemove(self)
end

function TransformGateControl:contentChanged(chain)
  if chain == self.branch then
    local outlet = chain:getMonitoringOutput(1)
    self.gateScope:watchOutlet(outlet)
    self.gateSub1:setText(chain:mnemonic())
  end
end

function TransformGateControl:setParamMode(enabled)
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

function TransformGateControl:onCursorEnter(spot)
  Base.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function TransformGateControl:onCursorLeave(spot)
  -- Decision 7: paramMode persists across leave/return. Clear only the
  -- per-session focus so the user has to deliberately re-focus to edit.
  self.focusedReadout = nil
  self:setSubCursorController(nil)
  self:releaseFocus("shiftPressed", "shiftReleased")
  Base.onCursorLeave(self, spot)
end

function TransformGateControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.focusedReadout then
    self.shiftSnapshot = self.focusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function TransformGateControl:shiftReleased()
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

function TransformGateControl:spotReleased(spot, shifted)
  -- Decision 2: no secondary toggle path on shift+spot. Just delegate.
  if Base.spotReleased(self, spot, shifted) then
    self:setFocusedReadout(nil)
    return true
  end
  return false
end

function TransformGateControl:subPressed(i, shifted)
  if shifted then return false end
  if self.paramMode then
    if i == 3 and not self.hasParamB then
      self.seq:fireTransform()
    end
  else
    if i == 3 then
      self.comparator:simulateRisingEdge()
    end
  end
  return true
end

function TransformGateControl:subReleased(i, shifted)
  if shifted then return false end

  if self.paramMode then
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
      if self.hasParamB then
        if self:hasFocus("encoder") then
          self:setFocusedReadout(self.paramBReadout)
        else
          self:focus()
          self:setFocusedReadout(self.paramBReadout)
        end
      end
    end
  else
    if i == 1 then
      -- Open branch for patching
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

function TransformGateControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    if self.focusedReadout == self.funcReadout then
      local val = math.floor(self.funcReadout:getValueInUnits() + 0.5)
      local name = self.funcNames[val]
      if name then
        self.funcLabel:setText(name)
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
