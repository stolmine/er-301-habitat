local app = app
local Env = require "Env"
local Class = require "Base.Class"
local ViewControl = require "Unit.ViewControl"
local Scales = require "core.Quantizer.Scales"
local ply = app.SECTION_PLY

local ScaleControl = Class {}
ScaleControl:include(ViewControl)

function ScaleControl:init(args)
  local name = args.name or "scale"
  local width = args.width or ply

  ViewControl.init(self, name)
  self:setClassName("ScaleControl")

  local graphic = app.Graphic(0, 0, width, 64)
  self.scaleList = app.SlidingList(0, 0, width, 64)
  graphic:addChild(self.scaleList)
  self:setMainCursorController(self.scaleList)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor {
    center = 0.5 * ply
  }

  self.filterbank = args.filterbank
  self.filterbankR = args.filterbankR

  -- Populate immediately like PitchCircle does
  self:refreshScales()
end

function ScaleControl:refreshScales()
  local selectedText = self.scaleList:selectedText()
  self.scaleList:clear()
  local order = Scales.getKeys()
  for i, name in ipairs(order) do
    local scale = Scales.getScale(name)
    if scale and #scale > 0 then
      self.scaleList:add(name)
    end
  end
  if selectedText then
    self.scaleList:select(selectedText)
  end
end

function ScaleControl:applyCurrentScale()
  local name = self.scaleList:selectedText()
  if not name or not self.filterbank then return end

  local scale = Scales.getScale(name)
  if not scale then return end

  local function loadInto(op)
    op:beginCustomScale()
    for _, cents in ipairs(scale) do
      if cents > 0 and cents <= 1200 then
        op:addCustomDegree(cents)
      end
    end
    op:endCustomScale()
  end

  loadInto(self.filterbank)
  if self.filterbankR then
    loadInto(self.filterbankR)
  end
end

function ScaleControl:onFocused()
  self:refreshScales()
end

function ScaleControl:serialize()
  local t = ViewControl.serialize(self)
  t.selectedScale = self.scaleList:selectedText()
  return t
end

function ScaleControl:deserialize(t)
  ViewControl.deserialize(self, t)
  if t.selectedScale then
    if self.scaleList:select(t.selectedScale) then
      self:applyCurrentScale()
    end
  end
end

local threshold = Env.EncoderThreshold.SlidingList
function ScaleControl:encoder(change, shifted)
  if self.scaleList:encoder(change, shifted, threshold) then
    self:applyCurrentScale()
  end
  return true
end

return ScaleControl
