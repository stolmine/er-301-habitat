-- DrumVoiceRandomGateControl -- Ngoma's randomize xform ply.
--
-- Pattern matches TransformGateControl's structure (main = ComparatorView,
-- paramMode swap for the sub-display) but with simplified semantics:
-- randomize-only (no function selector), two continuous faders
-- (depth, destination), fire on sub3. No paramB complication.
--
-- Gate view (paramMode=false, default):
--   Main: ComparatorView on the CV input.
--   Sub-display: input / thresh / fire gate-flow layout (same as stock Gate
--   + TransformGateControl).
--   Sub3 press simulates a rising edge -> C++ DrumVoice::applyRandomize.
--
-- Param view (paramMode=true, shift-toggled):
--   Main: same ComparatorView; cube-style indicator drawn in sub area.
--   Sub-display: depth (sub1) / dest (sub2) / fire! (sub3). The dest
--   readout is an integer 0..3 selecting which params get randomized:
--   0=all, 1=-swp (no Sweep+SweepTime), 2=-pch (also no Octave),
--   3=tmbr (Char/Shape/Grit/Punch only). C++ side branches on this.
--   Sub3 fires via op:fireRandomize() which sets the manual-fire flag;
--   C++ picks it up and randomizes on the next audio block.
--
-- Applies shift-audit Decision 1 (shiftUsed suppression), Decision 2
-- (no shift+spot toggle), Decision 3 (subGraphic swap, no show/hide),
-- Decision 5 (shift+sub opens keyboard in paramMode for depth/dest),
-- Decision 7 (persist paramMode across cursor leave, clear
-- paramFocusedReadout on leave), Decision 8 (paramModeDefaultSub unset
-- since neither depth nor dest shares a Bias with any main-fader).

local app = app
local Class = require "Base.Class"
local Base = require "Unit.ViewControl.EncoderControl"
local Encoder = require "Encoder"
local ShiftHelpers = require "spreadsheet.ShiftHelpers"

local ply = app.SECTION_PLY
local line1 = app.GRID5_LINE1
local line4 = app.GRID5_LINE4
local center1 = app.GRID5_CENTER1
local center3 = app.GRID5_CENTER3
local center4 = app.GRID5_CENTER4
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER

-- Gate-mode sub-display flow diagram (same as TransformGateControl).
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

-- Param-mode decoration: single "fire" arrow from sub3 up to title bar.
local paramInstructions = app.DrawingInstructions()
paramInstructions:circle(col3, center3, 8)
paramInstructions:vline(col3, line4, center3 - 8)
paramInstructions:triangle(col3, center3 - 11, 90, 3)
paramInstructions:vline(col3, center3 + 8, line1 - 2)
paramInstructions:triangle(col3, line1 - 2, 90, 3)

local DrumVoiceRandomGateControl = Class {
  type = "DrumVoiceRandomGateControl",
  canEdit = false,
  canMove = true
}
DrumVoiceRandomGateControl:include(Base)

function DrumVoiceRandomGateControl:init(args)
  local button = args.button or "xform"
  local description = args.description or "Randomize"
  local branch = args.branch or app.logError("%s.init: branch missing.", self)
  local comparator = args.comparator or app.logError("%s.init: comparator missing.", self)
  local op = args.op or app.logError("%s.init: op missing.", self)
  local depthParam = args.depthParam or app.logError("%s.init: depthParam missing.", self)
  local destParam = args.destParam or app.logError("%s.init: destParam missing.", self)

  Base.init(self, button)
  self:setClassName("DrumVoiceRandomGateControl")

  self.branch = branch
  self.comparator = comparator
  self.op = op
  self.paramMode = false
  self.shiftHeld = false
  self.shiftUsed = false

  -- Main graphic: ComparatorView (same as stock Gate + xform plies).
  local graphic = app.ComparatorView(0, 0, ply, 64, comparator)
  graphic:setLabel(button)
  self.comparatorView = graphic
  self:setMainCursorController(graphic)
  self:setControlGraphic(graphic)
  self:addSpotDescriptor { center = 0.5 * ply }

  -- Two sub-graphics; swap via setParamMode (Decision 3).
  self.normalSubGraphic = app.Graphic(0, 0, 128, 64)
  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  ---- GATE (normal) sub-display ----

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

  ---- PARAM sub-display ----

  self.paramDrawing = app.Drawing(0, 0, 128, 64)
  self.paramDrawing:add(paramInstructions)
  self.paramSubGraphic:addChild(self.paramDrawing)

  local unitMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  local function makeReadout(param, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(app.unitNone, unitMap)
    g:setPrecision(2)
    g:setCenter(x, center4)
    return g
  end

  self.depthReadout = makeReadout(depthParam, col1)

  -- Destination readout: integer 0..3 with named labels indicating
  -- which params get randomized. Mirrors the C++ applyRandomize switch.
  local destMap = (function()
    local m = app.LinearDialMap(0, 3)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()
  self.destReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(destParam)
    g:setAttributes(app.unitNone, destMap)
    g:setPrecision(0)
    g:setCenter(col2, center4)
    if g.addName then
      for _, v in ipairs({"all", "-swp", "-pch", "tmbr"}) do
        g:addName(v)
      end
    end
    return g
  end)()

  self.paramDesc = app.Label("Randomize", 10)
  self.paramDesc:fitToText(3)
  self.paramDesc:setSize(ply * 2, self.paramDesc.mHeight)
  self.paramDesc:setBorder(1)
  self.paramDesc:setCornerRadius(3, 0, 0, 3)
  self.paramDesc:setCenter(0.5 * (col2 + col3), center1 + 1)
  self.paramSubGraphic:addChild(self.paramDesc)

  self.paramSub1 = app.SubButton("depth", 1)
  self.paramSub2 = app.SubButton("dest", 2)
  self.paramSub3 = app.SubButton("fire!", 3)
  self.paramSubGraphic:addChild(self.depthReadout)
  self.paramSubGraphic:addChild(self.destReadout)
  self.paramSubGraphic:addChild(self.paramSub1)
  self.paramSubGraphic:addChild(self.paramSub2)
  self.paramSubGraphic:addChild(self.paramSub3)

  -- Default: gate mode (stock-side). Matches TransformGateControl init.
  self.subGraphic = self.normalSubGraphic

  branch:subscribe("contentChanged", self)
end

function DrumVoiceRandomGateControl:onRemove()
  self.branch:unsubscribe("contentChanged", self)
  Base.onRemove(self)
end

function DrumVoiceRandomGateControl:contentChanged(chain)
  if chain == self.branch then
    local outlet = chain:getMonitoringOutput(1)
    self.gateScope:watchOutlet(outlet)
    self.gateSub1:setText(chain:mnemonic())
  end
end

function DrumVoiceRandomGateControl:setParamMode(enabled)
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

function DrumVoiceRandomGateControl:setFocusedReadout(readout)
  if readout then readout:save() end
  self.focusedReadout = readout
  self:setSubCursorController(readout)
end

function DrumVoiceRandomGateControl:zeroPressed()
  if self.focusedReadout then self.focusedReadout:zero() end
  return true
end

function DrumVoiceRandomGateControl:cancelReleased(shifted)
  if self.focusedReadout then self.focusedReadout:restore() end
  return true
end

function DrumVoiceRandomGateControl:onCursorEnter(spot)
  Base.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function DrumVoiceRandomGateControl:onCursorLeave(spot)
  -- Decision 7: paramMode persists; clear only the per-session focus.
  self.focusedReadout = nil
  self:setSubCursorController(nil)
  self:releaseFocus("shiftPressed", "shiftReleased")
  Base.onCursorLeave(self, spot)
end

function DrumVoiceRandomGateControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.focusedReadout then
    self.shiftSnapshot = self.focusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function DrumVoiceRandomGateControl:shiftReleased()
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

function DrumVoiceRandomGateControl:spotReleased(spot, shifted)
  -- Decision 2: no secondary toggle path.
  if Base.spotReleased(self, spot, shifted) then
    self:setFocusedReadout(nil)
    return true
  end
  return false
end

function DrumVoiceRandomGateControl:subPressed(i, shifted)
  if shifted then return false end
  if i == 3 then
    -- Both modes fire. In gate mode this simulates a rising edge on
    -- the comparator (audible via ComparatorView too). In param mode,
    -- the C++ op's manual-fire flag is set directly.
    if self.paramMode then
      self.op:fireRandomize()
    else
      self.comparator:simulateRisingEdge()
    end
  end
  return true
end

function DrumVoiceRandomGateControl:subReleased(i, shifted)
  if shifted then return false end

  if self.paramMode then
    local readout = nil
    local label = nil
    if i == 1 then readout, label = self.depthReadout, "depth"
    elseif i == 2 then readout, label = self.destReadout, "destination"
    end
    if readout then
      if shifted then
        ShiftHelpers.openKeyboardFor(readout, label)
      else
        if self:hasFocus("encoder") then
          self:setFocusedReadout(readout)
        else
          self:focus()
          self:setFocusedReadout(readout)
        end
      end
    end
    -- i == 3 handled in subPressed for immediate fire.
    return true
  else
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

function DrumVoiceRandomGateControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.focusedReadout then
    self.focusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return false
end

function DrumVoiceRandomGateControl:upReleased(shifted)
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

return DrumVoiceRandomGateControl
