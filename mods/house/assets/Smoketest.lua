-- Phase 0 smoke-test unit for KWoodRoomDSP. Insert on hardware,
-- check the device log for [Smoketest] PASS / FAIL after a few
-- hundred ms of audio. Hard gate before Phase 1 (real kWoodRoom
-- unit).
--
-- See planning/kwoodroom-port-plan.md for context.

local app = app
local libhouse = require "house.libhouse"
local Class = require "Base.Class"
local Unit = require "Unit"

local Smoketest = Class {}
Smoketest:include(Unit)

function Smoketest:init(args)
  args.title = "Smoketest"
  args.mnemonic = "St"
  Unit.init(self, args)
end

function Smoketest:onLoadGraph(channelCount)
  local op = self:addObject("op", libhouse.Smoketest())
  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end
end

function Smoketest:onLoadViews()
  return {}, {
    expanded = {},
    collapsed = {}
  }
end

return Smoketest
