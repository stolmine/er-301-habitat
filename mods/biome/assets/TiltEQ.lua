local app = app
local libstolmine = require "biome.libbiome"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Encoder = require "Encoder"

local TiltEQ = Class {}
TiltEQ:include(Unit)

function TiltEQ:init(args)
  args.title = "Tilt EQ"
  args.mnemonic = "TQ"
  Unit.init(self, args)
end

function TiltEQ:onLoadGraph(channelCount)
  local eq1 = self:addObject("eq1", libstolmine.TiltEQ())
  local tilt = self:addObject("tilt", app.ParameterAdapter())
  tilt:hardSet("Bias", 0.0)

  connect(self, "In1", eq1, "In")
  connect(eq1, "Out", self, "Out1")
  tie(eq1, "Tilt", tilt, "Out")

  if channelCount > 1 then
    local eq2 = self:addObject("eq2", libstolmine.TiltEQ())
    connect(self, "In2", eq2, "In")
    connect(eq2, "Out", self, "Out2")
    tie(eq2, "Tilt", tilt, "Out")
  end

  self:addMonoBranch("tilt", tilt, "In", tilt, "Out")
end

local views = {
  expanded = { "tilt" },
  collapsed = {}
}

local function tiltMap()
  local m = app.LinearDialMap(-1, 1)
  m:setSteps(0.5, 0.1, 0.01, 0.001)
  return m
end

function TiltEQ:onLoadViews(objects, branches)
  local controls = {}

  controls.tilt = GainBias {
    button = "tilt",
    branch = branches.tilt,
    description = "Tilt",
    gainbias = objects.tilt,
    range = objects.tilt,
    biasMap = tiltMap(),
    biasUnits = app.unitNone,
    biasPrecision = 2,
    initialBias = 0.0
  }

  return controls, views
end

return TiltEQ
