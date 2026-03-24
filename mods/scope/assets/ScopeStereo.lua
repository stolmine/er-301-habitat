local app = app
local libscope = require "scope.libscope"
local Class = require "Base.Class"
local Unit = require "Unit"
local ViewControl = require "Unit.ViewControl"
local ply = app.SECTION_PLY

local ScopeStereo = Class {}
ScopeStereo:include(Unit)

function ScopeStereo:init(args)
  args.title = "Scope Stereo"
  args.mnemonic = "SS"
  Unit.init(self, args)
end

function ScopeStereo:onLoadGraph(channelCount)
  local op = self:addObject("op", libscope.Scope())
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

function ScopeStereo:onLoadViews()
  local view = Class {}
  view:include(ViewControl)

  function view:init(args)
    ViewControl.init(self)
    self:setClassName("Scope.ScopeStereoView")
    local width = args.width
    local graphic = app.Graphic(0, 0, width, 64)
    self:setMainCursorController(graphic)
    self:setControlGraphic(graphic)

    for i = 1, (width // ply) do
      self:addSpotDescriptor{center = (i - 0.5) * ply}
    end

    local w1 = width // 2
    local w2 = width - w1

    local leftScope = app.MiniScope(0, 0, w1, 64)
    graphic:addChild(leftScope)
    leftScope:watchOutlet(args.outletL)

    local rightScope = app.MiniScope(w1, 0, w2, 64)
    graphic:addChild(rightScope)
    rightScope:watchOutlet(args.outletR)

    local labelL = app.Label("L")
    labelL:setJustification(app.justifyLeft)
    labelL:setForegroundColor(app.GRAY7)
    labelL:setPosition(2, 51)
    graphic:addChild(labelL)

    local labelR = app.Label("R")
    labelR:setJustification(app.justifyLeft)
    labelR:setForegroundColor(app.GRAY7)
    labelR:setPosition(w1 + 2, 51)
    graphic:addChild(labelR)
  end

  local scopeView = view {
    width = 2 * ply,
    outletL = self.objects.op:getOutput("Out L"),
    outletR = self.objects.op:getOutput("Out R")
  }

  return {
    scope = scopeView
  }, {
    expanded = {"scope"},
    collapsed = {}
  }
end

return ScopeStereo
