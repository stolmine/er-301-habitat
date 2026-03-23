local app = app
local libgrids = require "grids.libgrids"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local GridsCircle = require "grids.GridsCircle"
local OptionControl = require "Unit.MenuControl.OptionControl"
local Encoder = require "Encoder"

local Grids = Class {}
Grids:include(Unit)

function Grids:init(args)
  args.title = "Grids"
  args.mnemonic = "Gr"
  Unit.init(self, args)
end

function Grids:onLoadGraph(channelCount)
  local op = self:addObject("op", libgrids.Grids())

  -- Clock from In1
  connect(self, "In1", op, "Clock")

  -- Reset
  local reset = self:addObject("reset", app.Comparator())
  reset:setMode(app.COMPARATOR_TRIGGER_ON_RISE)
  connect(reset, "Out", op, "Reset")
  self:addMonoBranch("reset", reset, "In", reset, "Out")

  -- Map X
  local mapx = self:addObject("mapx", app.ParameterAdapter())
  mapx:hardSet("Bias", 0.5)
  tie(op, "Map X", mapx, "Out")
  self:addMonoBranch("mapx", mapx, "In", mapx, "Out")

  -- Map Y
  local mapy = self:addObject("mapy", app.ParameterAdapter())
  mapy:hardSet("Bias", 0.5)
  tie(op, "Map Y", mapy, "Out")
  self:addMonoBranch("mapy", mapy, "In", mapy, "Out")

  -- Density
  local density = self:addObject("density", app.ParameterAdapter())
  density:hardSet("Bias", 0.5)
  tie(op, "Density", density, "Out")
  self:addMonoBranch("density", density, "In", density, "Out")

  -- Chaos
  local chaos = self:addObject("chaos", app.ParameterAdapter())
  chaos:hardSet("Bias", 0.0)
  tie(op, "Chaos", chaos, "Out")
  self:addMonoBranch("chaos", chaos, "In", chaos, "Out")

  -- Width
  local width = self:addObject("width", app.ParameterAdapter())
  width:hardSet("Bias", 0.5)
  tie(op, "Width", width, "Out")
  self:addMonoBranch("width", width, "In", width, "Out")

  -- Channel
  local channel = self:addObject("channel", app.ParameterAdapter())
  channel:hardSet("Bias", 0)
  tie(op, "Channel", channel, "Out")
  self:addMonoBranch("channel", channel, "In", channel, "Out")

  -- Output
  for i = 1, channelCount do
    connect(op, "Out", self, "Out"..i)
  end
end

function Grids:onShowMenu(objects)
  return {
    mode = OptionControl {
      description = "Output Mode",
      option      = objects.op:getOption("Mode"),
      choices     = { "trigger", "gate", "through" }
    },
  }, { "mode" }
end

function Grids:onLoadViews()
  return {
    circle = GridsCircle {
      description = "Grids",
      width       = 2 * app.SECTION_PLY,
      grids       = self.objects.op,
      mapx        = self.objects.mapx:getParameter("Bias"),
      mapy        = self.objects.mapy:getParameter("Bias"),
      density     = self.objects.density:getParameter("Bias")
    },
    reset = Gate {
      button      = "reset",
      description = "Reset",
      branch      = self.branches.reset,
      comparator  = self.objects.reset
    },
    mapx = GainBias {
      button        = "x",
      description   = "Map X",
      branch        = self.branches.mapx,
      gainbias      = self.objects.mapx,
      range         = self.objects.mapx,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    mapy = GainBias {
      button        = "y",
      description   = "Map Y",
      branch        = self.branches.mapy,
      gainbias      = self.objects.mapy,
      range         = self.objects.mapy,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    density = GainBias {
      button        = "fill",
      description   = "Density",
      branch        = self.branches.density,
      gainbias      = self.objects.density,
      range         = self.objects.density,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    chaos = GainBias {
      button        = "chaos",
      description   = "Chaos",
      branch        = self.branches.chaos,
      gainbias      = self.objects.chaos,
      range         = self.objects.chaos,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.0
    },
    width = GainBias {
      button        = "width",
      description   = "Width",
      branch        = self.branches.width,
      gainbias      = self.objects.width,
      range         = self.objects.width,
      biasMap       = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias   = 0.5
    },
    channel = GainBias {
      button        = "inst",
      description   = "Channel",
      branch        = self.branches.channel,
      gainbias      = self.objects.channel,
      range         = self.objects.channel,
      biasMap       = (function()
        local map = app.LinearDialMap(0, 2)
        map:setSteps(1, 1, 1, 1)
        map:setRounding(1)
        return map
      end)(),
      biasUnits     = app.unitNone,
      biasPrecision = 0,
      initialBias   = 0
    }
  }, {
    expanded  = { "circle" },
    circle    = { "circle", "channel", "reset", "mapx", "mapy", "density", "chaos", "width" },
    collapsed = { "circle" }
  }
end

return Grids
