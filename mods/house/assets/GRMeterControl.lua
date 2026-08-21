-- GRMeterControl -- a ply that shows gain reduction on a sideways VU
-- dial and drives Threshold with the encoder.
--
-- The meter shares the ply with the control it belongs to rather than
-- taking a slot of its own: a compressor's meter is only meaningful
-- next to the threshold that causes the reduction, and plies are the
-- scarce resource (SECTION_PLY is 42 px against a 256 px display, so
-- only six are visible).
local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"
local libhouse = require "house.libhouse"

local ply = app.SECTION_PLY

local GRMeterControl = Class {}
GRMeterControl:include(GainBias)

function GRMeterControl:init(args)
  GainBias.init(self, args)
  self:setClassName("GRMeterControl")

  local meter = libhouse.GainReductionGraphic(0, 0, ply, 64)
  meter:follow(args.op)
  self.meter = meter

  -- Wrap rather than replace, so the encoder, range and readout stay
  -- standard GainBias behaviour.
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(meter)
  self:setControlGraphic(container)
  self:setMainCursorController(self.fader)
end

return GRMeterControl
