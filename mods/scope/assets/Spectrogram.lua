local app = app
local libscope = require "scope.libscope"
local Class = require "Base.Class"
local Unit = require "Unit"
local ViewControl = require "Unit.ViewControl"
local ply = app.SECTION_PLY

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

  return {
    spectrum = specView
  }, {
    expanded = {"spectrum"},
    collapsed = {}
  }
end

return Spectrogram
