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

local BandControl = Class {}
BandControl:include(GainBias)

function BandControl:init(args)
  GainBias.init(self, args)

  -- Replace fader graphic with spectrum display
  if args.dspObject and args.bandIndex then
    local spectrum = libspreadsheet.SpectrumGraphic(0, 0, ply, 64)
    spectrum:follow(args.dspObject)
    spectrum:setBandIndex(args.bandIndex)
    local container = app.Graphic(0, 0, ply, 64)
    container:addChild(spectrum)
    self:setMainCursorController(spectrum)
    self:setControlGraphic(container)
  end

  self.paramMode = 0  -- 0=normal, 1=shaper SD, 2=filter SD
  self.shiftHeld = false
  self.shiftUsed = false
  self.normalSubGraphic = self.subGraphic

  local function makeReadout(param, map, precision, x)
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(param)
    g:setAttributes(app.unitNone, map)
    g:setPrecision(precision)
    g:setCenter(x, center4)
    return g
  end

  -- Shaper sub-display: amt / bias / type
  local amtMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  local biasMap = (function()
    local m = app.LinearDialMap(-1, 1)
    m:setSteps(0.1, 0.01, 0.001, 0.001)
    return m
  end)()

  local typeMap = (function()
    local m = app.LinearDialMap(0, 6)
    m:setSteps(1, 1, 1, 1)
    m:setRounding(1)
    return m
  end)()

  self.shaperSubGraphic = app.Graphic(0, 0, 128, 64)

  self.amtReadout = makeReadout(args.amount, amtMap, 2, col1)
  self.biasReadout = makeReadout(args.bias, biasMap, 2, col2)
  self.typeReadout = makeReadout(args.shaperType, typeMap, 0, col3)
  if self.typeReadout.addName then
    for _, v in ipairs({"soft", "hard", "fold", "rect", "crsh", "sine", "poly"}) do
      self.typeReadout:addName(v)
    end
  end

  local shaperDesc = app.Label("Amt / Bias / Type", 10)
  shaperDesc:fitToText(3)
  shaperDesc:setSize(ply * 3, shaperDesc.mHeight)
  shaperDesc:setBorder(1)
  shaperDesc:setCornerRadius(3, 0, 0, 3)
  shaperDesc:setCenter(col2, center1 + 1)

  self.shaperSubGraphic:addChild(self.amtReadout)
  self.shaperSubGraphic:addChild(self.biasReadout)
  self.shaperSubGraphic:addChild(self.typeReadout)
  self.shaperSubGraphic:addChild(shaperDesc)
  self.shaperSubGraphic:addChild(app.SubButton("amt", 1))
  self.shaperSubGraphic:addChild(app.SubButton("bias", 2))
  self.shaperSubGraphic:addChild(app.SubButton("type", 3))

  -- Filter sub-display: wt / freq / morph
  local weightMap = (function()
    local m = app.LinearDialMap(0.1, 4)
    m:setSteps(0.5, 0.1, 0.01, 0.01)
    return m
  end)()

  local freqMap = (function()
    local m = app.LinearDialMap(20, 20000)
    m:setSteps(1000, 100, 10, 1)
    return m
  end)()

  self.filterSubGraphic = app.Graphic(0, 0, 128, 64)

  self.weightReadout = makeReadout(args.weight, weightMap, 2, col1)
  self.freqReadout = makeReadout(args.filterFreq, freqMap, 0, col2)
  local morphMap = (function()
    local m = app.LinearDialMap(0, 1)
    m:setSteps(0.25, 0.05, 0.01, 0.001)
    return m
  end)()

  self.morphReadout = makeReadout(args.filterMorph, morphMap, 2, col3)
  if self.morphReadout.addName then
    for _, v in ipairs({"off", "LP", "BP", "HP", "ntch"}) do
      self.morphReadout:addName(v)
    end
  end

  local filterDesc = app.Label("Wt / Freq / Morph", 10)
  filterDesc:fitToText(3)
  filterDesc:setSize(ply * 3, filterDesc.mHeight)
  filterDesc:setBorder(1)
  filterDesc:setCornerRadius(3, 0, 0, 3)
  filterDesc:setCenter(col2, center1 + 1)

  self.filterSubGraphic:addChild(self.weightReadout)
  self.filterSubGraphic:addChild(self.freqReadout)
  self.filterSubGraphic:addChild(self.morphReadout)
  self.filterSubGraphic:addChild(filterDesc)
  self.filterSubGraphic:addChild(app.SubButton("wt", 1))
  self.filterSubGraphic:addChild(app.SubButton("freq", 2))
  self.filterSubGraphic:addChild(app.SubButton("mrph", 3))
end

function BandControl:setParamMode(mode)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = mode
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)

  if mode == 1 then
    self.subGraphic = self.shaperSubGraphic
  elseif mode == 2 then
    self.subGraphic = self.filterSubGraphic
  else
    self.subGraphic = self.normalSubGraphic
    self:setFocusedReadout(self.bias)
  end
  self:addSubGraphic(self.subGraphic)
end

function BandControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function BandControl:onCursorLeave(spot)
  if self.paramMode ~= 0 then
    self:removeSubGraphic(self.subGraphic)
    self.paramMode = 0
    self.subGraphic = self.normalSubGraphic
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function BandControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function BandControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.paramFocusedReadout and self.shiftSnapshot then
      local cur = self.paramFocusedReadout:getValueInUnits()
      if cur ~= self.shiftSnapshot then
        self.shiftHeld = false
        self.shiftSnapshot = nil
        return true
      end
    end
    -- Cycle: normal -> shaper -> filter -> normal
    local next = (self.paramMode + 1) % 3
    self:setParamMode(next)
  end
  self.shiftHeld = false
  self.shiftSnapshot = nil
  return true
end

function BandControl:spotReleased(spot, shifted)
  if self.paramMode ~= 0 then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(0)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function BandControl:subReleased(i, shifted)
  if shifted then return false end
  if self.paramMode == 1 then
    local readout = i == 1 and self.amtReadout
        or i == 2 and self.biasReadout
        or i == 3 and self.typeReadout or nil
    if readout then
      readout:save()
      self.paramFocusedReadout = readout
      self:setSubCursorController(readout)
      if not self:hasFocus("encoder") then self:focus() end
    end
    return true
  elseif self.paramMode == 2 then
    local readout = i == 1 and self.weightReadout
        or i == 2 and self.freqReadout
        or i == 3 and self.morphReadout or nil
    if readout then
      readout:save()
      self.paramFocusedReadout = readout
      self:setSubCursorController(readout)
      if not self:hasFocus("encoder") then self:focus() end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function BandControl:encoder(change, shifted)
  if shifted and self.shiftHeld then
    self.shiftUsed = true
  end
  if self.paramMode ~= 0 and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function BandControl:zeroPressed()
  if self.paramMode ~= 0 and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function BandControl:cancelReleased(shifted)
  if self.paramMode ~= 0 and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return BandControl
