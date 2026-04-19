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

local SomScanControl = Class {}
SomScanControl:include(GainBias)

function SomScanControl:init(args)
  GainBias.init(self, args)

  local sphere = libspreadsheet.SomSphereGraphic(0, 0, ply, 64)
  sphere:follow(args.op)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(sphere)
  self:setMainCursorController(sphere)
  self:setControlGraphic(container)

  self.paramMode = true
  self.shiftHeld = false
  self.shiftUsed = false
  self.levelSubGraphic = self.subGraphic

  self.paramSubGraphic = app.Graphic(0, 0, 128, 64)

  local desc = app.Label("SOM Scan", 10)
  desc:fitToText(3)
  desc:setSize(ply * 3, desc.mHeight)
  desc:setBorder(1)
  desc:setCornerRadius(3, 0, 0, 3)
  desc:setCenter(col2, center1 + 1)
  self.paramSubGraphic:addChild(desc)

  local nbrMap = (function()
    local m = app.LinearDialMap(0.05, 0.5)
    m:setSteps(0.05, 0.01, 0.001, 0.001)
    return m
  end)()

  local rateMap = (function()
    local m = app.LinearDialMap(0.01, 1)
    m:setSteps(0.05, 0.01, 0.001, 0.001)
    return m
  end)()

  local decayMap = (function()
    local m = app.LinearDialMap(0.9, 1.0)
    m:setSteps(0.01, 0.001, 0.0001, 0.0001)
    return m
  end)()

  self.decayReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.decayParam)
    g:setAttributes(app.unitNone, decayMap)
    g:setPrecision(3)
    g:setCenter(col1, center4)
    if g.useHardSet then g:useHardSet() end
    return g
  end)()

  self.nbrReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.nbrParam)
    g:setAttributes(app.unitNone, nbrMap)
    g:setPrecision(2)
    g:setCenter(col2, center4)
    if g.useHardSet then g:useHardSet() end
    return g
  end)()

  self.rateReadout = (function()
    local g = app.Readout(0, 0, ply, 10)
    g:setParameter(args.rateParam)
    g:setAttributes(app.unitNone, rateMap)
    g:setPrecision(2)
    g:setCenter(col3, center4)
    if g.useHardSet then g:useHardSet() end
    return g
  end)()

  self.paramSubGraphic:addChild(self.decayReadout)
  self.paramSubGraphic:addChild(self.nbrReadout)
  self.paramSubGraphic:addChild(self.rateReadout)
  self.paramSubGraphic:addChild(app.SubButton("decay", 1))
  self.paramSubGraphic:addChild(app.SubButton("nbr", 2))
  self.paramSubGraphic:addChild(app.SubButton("rate", 3))

  self:setParamMode(true)
end

function SomScanControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.paramSubGraphic
  else
    self.subGraphic = self.levelSubGraphic
    self:setFocusedReadout(self.bias)
  end
  self:addSubGraphic(self.subGraphic)
end

function SomScanControl:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function SomScanControl:onCursorLeave(spot)
  if not self.paramMode then
    self:removeSubGraphic(self.subGraphic)
    self.paramMode = true
    self.subGraphic = self.paramSubGraphic
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function SomScanControl:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function SomScanControl:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.paramFocusedReadout and self.shiftSnapshot then
      local cur = self.paramFocusedReadout:getValueInUnits()
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

function SomScanControl:focusParamReadout(readout)
  readout:save()
  self.paramFocusedReadout = readout
  self:setSubCursorController(readout)
  if not self:hasFocus("encoder") then self:focus() end
end

function SomScanControl:subReleased(i, shifted)
  if shifted then return false end
  if self.paramMode then
    if i == 1 then self:focusParamReadout(self.decayReadout)
    elseif i == 2 then self:focusParamReadout(self.nbrReadout)
    elseif i == 3 then self:focusParamReadout(self.rateReadout)
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function SomScanControl:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted, self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function SomScanControl:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function SomScanControl:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end

return SomScanControl
