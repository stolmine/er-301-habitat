local app = app
local libwarps = require "warps.libwarps"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local BranchMeter = require "Unit.ViewControl.BranchMeter"
local AlgoSelector = require "warps.AlgoSelector"
local MenuHeader = require "Unit.MenuControl.Header"
local Task = require "Unit.MenuControl.Task"
local Encoder = require "Encoder"

local algoMap = app.LinearDialMap(0, 1.0)
algoMap:setSteps(0.125, 0.01, 0.001, 0.0001)

local algoNames = {
  "XFade", "Fold", "AnaRM", "DigRM",
  "XOR", "Compar", "Xtion", "Vocod", "Vocod+"
}

local function getAlgoName(value)
  local idx = math.floor(value * 8 + 0.5)
  if idx < 0 then idx = 0 end
  if idx > 8 then idx = 8 end
  return algoNames[idx + 1] or "XFade"
end

local Warps = Class {}
Warps:include(Unit)

function Warps:init(args)
  args.title = "Warps"
  args.mnemonic = "Wp"
  Unit.init(self, args)
end

function Warps:onLoadGraph(channelCount)
  local warps = self:addObject("warps", libwarps.WarpsModulator())

  -- Carrier: fed by the unit's chain input (In1)
  local car = self:addObject("car", app.ConstantGain())
  car:hardSet("Gain", 1.0)
  car:setClampInDecibels(-59.9)
  connect(self, "In1", car, "In")
  connect(car, "Out", warps, "Carrier")

  -- Modulator: fed via branch sub-chain
  local mod = self:addObject("mod", app.ConstantGain())
  mod:hardSet("Gain", 1.0)
  mod:setClampInDecibels(-59.9)
  self:addMonoBranch("mod", mod, "In", mod, "Out")
  connect(mod, "Out", warps, "Modulator")

  -- Algorithm
  local algo = self:addObject("algo", app.ParameterAdapter())
  algo:hardSet("Bias", 0)
  tie(warps, "Algorithm", algo, "Out")
  self:addMonoBranch("algo", algo, "In", algo, "Out")

  -- Timbre
  local timbre = self:addObject("timbre", app.ParameterAdapter())
  timbre:hardSet("Bias", 0.5)
  tie(warps, "Timbre", timbre, "Out")
  self:addMonoBranch("timbre", timbre, "In", timbre, "Out")

  -- Drive
  local drive = self:addObject("drive", app.ParameterAdapter())
  drive:hardSet("Bias", 0.5)
  tie(warps, "Drive", drive, "Out")
  self:addMonoBranch("drive", drive, "In", drive, "Out")

  -- Output
  connect(warps, "Out", self, "Out1")
  if channelCount > 1 then
    connect(warps, "Aux", self, "Out2")
  end
end

-- Config menu
local menu = {
  "easterEggHeader",
  "easterEgg"
}

function Warps:onShowMenu(objects, branches)
  local controls = {}

  local isEE = objects.warps:getOption("Easter Egg"):value() == 1

  controls.easterEggHeader = MenuHeader {
    description = isEE and "Freq Shifter: ON" or "Freq Shifter: OFF"
  }

  controls.easterEgg = Task {
    description = isEE and "disable" or "enable",
    task = function()
      if isEE then
        objects.warps:setOptionValue("Easter Egg", 0)
      else
        objects.warps:setOptionValue("Easter Egg", 1)
      end
    end
  }

  return controls, menu
end

local views = {
  expanded = {
    "mod",
    "algo",
    "timbre",
    "drive"
  },
  collapsed = {}
}

function Warps:onLoadViews(objects, branches)
  local controls = {}

  controls.mod = BranchMeter {
    button = "mod",
    branch = branches.mod,
    faderParam = objects.mod:getParameter("Gain")
  }

  controls.algo = AlgoSelector {
    button = "XFade",
    description = "Algorithm",
    branch = branches.algo,
    gainbias = objects.algo,
    range = objects.algo,
    biasMap = algoMap,
    biasPrecision = 2,
    initialBias = 0,
    algoNameFn = getAlgoName
  }

  controls.timbre = GainBias {
    button = "timb",
    description = "Timbre",
    branch = branches.timbre,
    gainbias = objects.timbre,
    range = objects.timbre,
    biasMap = Encoder.getMap("[0,1]"),
    biasPrecision = 2,
    initialBias = 0.5
  }

  controls.drive = GainBias {
    button = "drive",
    description = "Drive",
    branch = branches.drive,
    gainbias = objects.drive,
    range = objects.drive,
    biasMap = Encoder.getMap("[0,1]"),
    biasPrecision = 2,
    initialBias = 0.5
  }

  self:addToMuteGroup(controls.mod)

  return controls, views
end

return Warps
