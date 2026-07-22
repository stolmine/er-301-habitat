local app = app
local libscope = require "scope.libscope"
local Class = require "Base.Class"
local Unit = require "Unit"
local ViewControl = require "Unit.ViewControl"
local GainBias = require "Unit.ViewControl.GainBias"
local ply = app.SECTION_PLY

-- Resolution/ply picker: 1 (base 256, single window) .. 6. 2/3 add overlap-
-- averaging on the 256-pt FFT; 4/6 use a 512-pt FFT + overlap-averaging.
local function resolutionMap()
  local m = app.LinearDialMap(1, 6)
  m:setSteps(1, 1, 1, 1) -- integer snap
  return m
end

local Spectrogram = Class {}
Spectrogram:include(Unit)

function Spectrogram:init(args)
  args.title = "Spectrogram"
  args.mnemonic = "Sg"
  Unit.init(self, args)
end

function Spectrogram:onLoadGraph(channelCount)
  local op = self:addObject("op", libscope.Spectrogram())
  if channelCount > 1 then
    connect(self, "In1", op, "In L")
    connect(self, "In2", op, "In R")
    connect(op, "Out L", self, "Out1")
    connect(op, "Out R", self, "Out2")
  else
    connect(self, "In1", op, "In L")
    connect(op, "Out L", self, "Out1")
  end

  -- Resolution/ply (block-rate; not CV-driven).
  local res = self:addObject("res", app.ParameterAdapter())
  res:hardSet("Bias", 1.0)
  tie(op, "Resolution", res, "Out")
  self:addMonoBranch("res", res, "In", res, "Out")
end

function Spectrogram:onLoadViews()
  local view = Class {}
  view:include(ViewControl)

  function view:init(args)
    ViewControl.init(self)
    self:setClassName("Scope.SpectrogramView")
    local width = args.width
    local graphic = app.Graphic(0, 0, width, 64)
    self:setMainCursorController(graphic)
    self:setControlGraphic(graphic)

    for i = 1, (width // ply) do
      self:addSpotDescriptor{center = (i - 0.5) * ply}
    end

    local spectrum = libscope.SpectrogramGraphic(0, 0, width, 64)
    spectrum:follow(args.dspObject)
    graphic:addChild(spectrum)
  end

  local specView = view {
    width = 2 * ply,
    dspObject = self.objects.op
  }

  local resolution = GainBias {
    button = "res",
    branch = self.branches.res,
    description = "Resolution",
    gainbias = self.objects.res,
    range = self.objects.res,
    biasMap = resolutionMap(),
    biasUnits = app.unitNone,
    biasPrecision = 0,
    initialBias = 1.0
  }

  return {
    spectrum = specView,
    resolution = resolution
  }, {
    expanded = {"spectrum", "resolution"},
    collapsed = {}
  }
end

return Spectrogram
