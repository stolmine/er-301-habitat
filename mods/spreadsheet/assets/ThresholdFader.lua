local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"

local ThresholdFader = Class {}
ThresholdFader:include(GainBias)

function ThresholdFader:init(args)
  GainBias.init(self, args)
  if args.thresholdLabels and self.fader and self.fader.addThresholdLabel then
    for _, entry in ipairs(args.thresholdLabels) do
      self.fader:addThresholdLabel(entry[1], entry[2])
    end
  end
end

return ThresholdFader
