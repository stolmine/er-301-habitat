local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"

local ScanControl = Class {}
ScanControl:include(GainBias)

function ScanControl:init(args)
  self.op = args.op
  self.windowSize = args.windowSize or 256
  GainBias.init(self, args)
  self:updateLabel()
end

function ScanControl:updateLabel()
  if not self.op then return end
  if not self.bias then return end
  local dataSize = self.op:getDataSize()
  if dataSize < self.windowSize then
    self.fader:setLabel("no data")
    return
  end
  local scan = self.bias:getValueInUnits()
  if scan < 0 then scan = 0 end
  if scan > 1 then scan = 1 end
  local maxOffset = dataSize - self.windowSize
  local offset = math.floor(scan * maxOffset)
  self.fader:setLabel(string.format("0x%X", offset))
end

function ScanControl:encoder(change, shifted)
  GainBias.encoder(self, change, shifted)
  self:updateLabel()
  return true
end

function ScanControl:spotReleased(spot, shifted)
  local result = GainBias.spotReleased(self, spot, shifted)
  self:updateLabel()
  return result
end

function ScanControl:subReleased(i, shifted)
  local result = GainBias.subReleased(self, i, shifted)
  self:updateLabel()
  return result
end

return ScanControl
