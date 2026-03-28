local app = app
local libstolmine = require "stolmine.libstolmine"
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
  self.mathMode = false
  self.funcNames = args.funcNames or defaultFuncNames
  local funcMap = args.funcMap or defaultFuncMap

  -- Main graphic: ComparatorView (same as Gate)
  local graphic = app.ComparatorView(0, 0, ply, 64, comparator)
  graphic:setLabel(button)
  self.comparatorView = graphic
  self:setMainCursorController(graphic)
  self:setControlGraphic(graphic)
  self:addSpotDescriptor { center = 0.5 * ply }

  -- Single sub-graphic with both modes
  self.subGraphic = app.Graphic(0, 0, 128, 64)

  ---- GATE MODE ELEMENTS ----

  -- Flow diagram
  self.gateDrawing = app.Drawing(0, 0, 128, 64)
  self.gateDrawing:add(gateInstructions)
  self.subGraphic:addChild(self.gateDrawing)

  -- "or" label
  self.gateOrLabel = app.Label("or", 10)
  self.gateOrLabel:fitToText(0)
  self.gateOrLabel:setCenter(col3, center3 + 1)
  self.subGraphic:addChild(self.gateOrLabel)

  -- Scope watching branch input
  self.gateScope = app.MiniScope(col1 - 20, line4, 40, 45)
  self.gateScope:setBorder(1)
  self.gateScope:setCornerRadius(3, 3, 3, 3)
  self.subGraphic:addChild(self.gateScope)

  -- Threshold readout
  local threshParam = comparator:getParameter("Threshold")
  threshParam:enableSerialization()
  self.threshReadout = app.Readout(0, 0, ply, 10)
  self.threshReadout:setParameter(threshParam)
  self.threshReadout:setAttributes(app.unitNone, Encoder.getMap("default"))
  self.threshReadout:setCenter(col2, center4)
  self.subGraphic:addChild(self.threshReadout)

  -- Description label
  self.gateDesc = app.Label(description, 10)
  self.gateDesc:fitToText(3)
  self.gateDesc:setSize(ply * 2, self.gateDesc.mHeight)
  self.gateDesc:setBorder(1)
  self.gateDesc:setCornerRadius(3, 0, 0, 3)
  self.gateDesc:setCenter(0.5 * (col2 + col3), center1 + 1)
  self.subGraphic:addChild(self.gateDesc)

  -- Gate sub-buttons
  self.gateSub1 = app.SubButton("input", 1)
  self.gateSub2 = app.SubButton("thresh", 2)
  self.gateSub3 = app.SubButton("fire", 3)
  self.subGraphic:addChild(self.gateSub1)
  self.subGraphic:addChild(self.gateSub2)
  self.subGraphic:addChild(self.gateSub3)

  ---- MATH MODE ELEMENTS ----

  -- Fire flow graphic
  self.mathDrawing = app.Drawing(0, 0, 128, 64)
  self.mathDrawing:add(mathInstructions)
  self.subGraphic:addChild(self.mathDrawing)

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

  -- Func label (shows name instead of number)
  self.funcLabel = app.Label(self.funcNames[0] or "add", 10)
  self.funcLabel:fitToText(0)
  self.funcLabel:setCenter(col1, center3 + 1)
  self.subGraphic:addChild(self.funcLabel)

  self.mathDesc = app.Label("Transform", 10)
  self.mathDesc:fitToText(3)
  self.mathDesc:setSize(ply * 2, self.mathDesc.mHeight)
  self.mathDesc:setBorder(1)
  self.mathDesc:setCornerRadius(3, 0, 0, 3)
  self.mathDesc:setCenter(0.5 * (col2 + col3), center1 + 1)

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

function TransformGateControl:setMathMode(enabled)
  self.mathMode = enabled

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

  -- Math elements
  if enabled then
    self.mathDrawing:show()
    self.funcReadout:show()
    self.factorReadout:show()
    self.funcLabel:show()
    self.mathDesc:show()
    self.mathSub1:show()
    self.mathSub2:show()
    self.mathSub3:show()
  else
    self.mathDrawing:hide()
    self.funcReadout:hide()
    self.factorReadout:hide()
    self.funcLabel:hide()
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

function TransformGateControl:spotReleased(spot, shifted)
  if shifted then
    self:setMathMode(not self.mathMode)
    return true
  end
  if Base.spotReleased(self, spot, shifted) then
    self:setFocusedReadout(nil)
    return true
  end
  return false
end

function TransformGateControl:subPressed(i, shifted)
  if shifted then return false end
  if self.mathMode then
    if i == 3 then
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
      -- Rising edge was in subPressed
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
