-- ScopeView: shared ViewControl for Scope / Scope 2x / Scope Stereo.
-- Owns one or two ScopeGraphic instances, plus a sub-display with
-- Time + Gain selectors driven by M1 / M2.
--
-- args:
--   width   = pixel width of the main graphic
--   outlet  = single-graphic outlet  (mono / wide)
--   outlets = { L, R }              (stereo: two graphics side by side)

local app = app
local libscope = require "scope.libscope"
local Class = require "Base.Class"
local ViewControl = require "Unit.ViewControl"
local Encoder = require "Encoder"
local ply = app.SECTION_PLY

local center3 = app.GRID5_CENTER3
local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER

local DECIMATION  = { 1, 2, 4, 8, 16, 32, 64 }
local TIME_LABELS = { "1x", "2x", "4x", "8x", "16x", "32x", "64x" }
local GAIN_VALUES = { 0.25, 0.5, 1.0, 2.0, 4.0 }
local GAIN_LABELS = { "0.25x", "0.5x", "1x", "2x", "4x" }

local TIME_DEFAULT = 2  -- "2x" matches firmware MiniScope default decimation
local GAIN_DEFAULT = 3  -- "1x"

local ScopeView = Class {}
ScopeView:include(ViewControl)

function ScopeView:init(args)
  ViewControl.init(self)
  self:setClassName("Scope.ScopeView")

  local width = args.width or ply

  local graphic = app.Graphic(0, 0, width, 64)
  self:setMainCursorController(graphic)
  self:setControlGraphic(graphic)
  for i = 1, (width // ply) do
    self:addSpotDescriptor{ center = (i - 0.5) * ply }
  end

  self.scopes = {}
  if args.outlets then
    local w1 = width // 2
    local w2 = width - w1
    local left  = libscope.ScopeGraphic(0,  0, w1, 64)
    local right = libscope.ScopeGraphic(w1, 0, w2, 64)
    graphic:addChild(left)
    graphic:addChild(right)
    left:watchOutlet(args.outlets[1])
    right:watchOutlet(args.outlets[2])
    self.scopes[1] = left
    self.scopes[2] = right

    local lLabel = app.Label("L", 10)
    lLabel:setJustification(app.justifyLeft)
    lLabel:setForegroundColor(app.GRAY7)
    lLabel:setPosition(2, 51)
    graphic:addChild(lLabel)

    local rLabel = app.Label("R", 10)
    rLabel:setJustification(app.justifyLeft)
    rLabel:setForegroundColor(app.GRAY7)
    rLabel:setPosition(w1 + 2, 51)
    graphic:addChild(rLabel)
  else
    local scope = libscope.ScopeGraphic(0, 0, width, 64)
    graphic:addChild(scope)
    scope:watchOutlet(args.outlet)
    self.scopes[1] = scope
  end

  -- Sub-display: Time / Gain / Volt readouts. Each value sits inside
  -- a ScopeControlBox that renders dotted when unfocused, solid when
  -- focused (Volt is read-only so its box stays dotted).
  self.subGraphic = app.Graphic(0, 0, 128, 64)

  self.timeBox = libscope.ScopeControlBox(0, 0, 22, 16)
  self.subGraphic:addChild(self.timeBox)

  self.timeLabel = app.Label(TIME_LABELS[TIME_DEFAULT], 12)
  self.timeLabel:setJustification(app.justifyCenter)
  self.timeLabel:setForegroundColor(app.WHITE)
  self.subGraphic:addChild(self.timeLabel)

  self.gainBox = libscope.ScopeControlBox(0, 0, 22, 16)
  self.subGraphic:addChild(self.gainBox)

  self.gainLabel = app.Label(GAIN_LABELS[GAIN_DEFAULT], 12)
  self.gainLabel:setJustification(app.justifyCenter)
  self.gainLabel:setForegroundColor(app.WHITE)
  self.subGraphic:addChild(self.gainLabel)

  -- Volt: wider box because "-9.999" is the longest readout.
  self.voltBox = libscope.ScopeControlBox(0, 0, 44, 16)
  self.voltBox:setCenter(col3, center3)
  self.subGraphic:addChild(self.voltBox)

  -- Read-only rolling-mean voltage. Right-justified inside its area
  -- so the decimal column stays put as digits change.
  local readout = libscope.ScopeVoltsReadout(0, 0, 40, 12)
  readout:follow(self.scopes[1])
  readout:setCenter(col3, center3)
  self.subGraphic:addChild(readout)
  self.voltsReadout = readout

  self.subGraphic:addChild(app.SubButton("time", 1))
  self.subGraphic:addChild(app.SubButton("gain", 2))
  self.subGraphic:addChild(app.SubButton("volt", 3))

  self.timeIdx = TIME_DEFAULT
  self.gainIdx = GAIN_DEFAULT
  self.focusedSlot = "time"
  self.encoderState = Encoder.Coarse

  self:applyTime()
  self:applyGain()
end

local function sizeBoxToLabel(box, label, col, row)
  -- fitToText sets the label's intrinsic size to its rendered text plus
  -- padding. Mirror that into the box (slightly larger so the border
  -- doesn't clip the glyphs) and re-center both at the column.
  label:fitToText(4)
  box:setSize(label.mWidth + 4, label.mHeight + 4)
  box:setCenter(col, row)
  label:setCenter(col, row)
end

function ScopeView:applyTime()
  local d = DECIMATION[self.timeIdx]
  for _, s in ipairs(self.scopes) do s:setDecimation(d) end
  self.timeLabel:setText(TIME_LABELS[self.timeIdx])
  sizeBoxToLabel(self.timeBox, self.timeLabel, col1, center3)
end

function ScopeView:applyGain()
  local g = GAIN_VALUES[self.gainIdx]
  for _, s in ipairs(self.scopes) do s:setGain(g) end
  self.gainLabel:setText(GAIN_LABELS[self.gainIdx])
  sizeBoxToLabel(self.gainBox, self.gainLabel, col2, center3)
end

function ScopeView:bumpTime(delta)
  local n = self.timeIdx + (delta > 0 and 1 or -1)
  if n < 1 then n = 1 end
  if n > #DECIMATION then n = #DECIMATION end
  if n ~= self.timeIdx then
    self.timeIdx = n
    self:applyTime()
  end
end

function ScopeView:bumpGain(delta)
  local n = self.gainIdx + (delta > 0 and 1 or -1)
  if n < 1 then n = 1 end
  if n > #GAIN_VALUES then n = #GAIN_VALUES end
  if n ~= self.gainIdx then
    self.gainIdx = n
    self:applyGain()
  end
end

function ScopeView:refreshFocusVisual()
  -- Solid border on the focused slot's box, dotted on the other two.
  -- Volt is read-only so it always renders dotted.
  self.timeBox:setFocused(self.focusedSlot == "time")
  self.gainBox:setFocused(self.focusedSlot == "gain")
end

function ScopeView:subReleased(i, shifted)
  if shifted then return false end
  if i == 1 then
    self.focusedSlot = "time"
  elseif i == 2 then
    self.focusedSlot = "gain"
  else
    return true
  end
  if not self:hasFocus("encoder") then self:focus() end
  self:refreshFocusVisual()
  return true
end

function ScopeView:encoder(change, shifted)
  if self.focusedSlot == "gain" then
    self:bumpGain(change)
  else
    self:bumpTime(change)
  end
  return true
end

function ScopeView:onCursorEnter(spot)
  ViewControl.onCursorEnter(self, spot)
  self:refreshFocusVisual()
end

function ScopeView:onCursorLeave(spot)
  self.timeBox:setFocused(false)
  self.gainBox:setFocused(false)
  ViewControl.onCursorLeave(self, spot)
end

function ScopeView:setTimeIdx(idx)
  if idx and idx >= 1 and idx <= #DECIMATION then
    self.timeIdx = idx
    self:applyTime()
  end
end

function ScopeView:setGainIdx(idx)
  if idx and idx >= 1 and idx <= #GAIN_VALUES then
    self.gainIdx = idx
    self:applyGain()
  end
end

function ScopeView:serialize()
  local t = ViewControl.serialize(self)
  t.timeIdx = self.timeIdx
  t.gainIdx = self.gainIdx
  return t
end

function ScopeView:deserialize(t)
  ViewControl.deserialize(self, t)
  if t.timeIdx then self:setTimeIdx(t.timeIdx) end
  if t.gainIdx then self:setGainIdx(t.gainIdx) end
end

return ScopeView
