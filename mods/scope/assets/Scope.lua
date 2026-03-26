local app = app
local libscope = require "scope.libscope"
local Class = require "Base.Class"
local Unit = require "Unit"
local ViewControl = require "Unit.ViewControl"
local ply = app.SECTION_PLY

local Scope = Class {}
Scope:include(Unit)

function Scope:init(args)
  args.title = "Scope"
  args.mnemonic = "Sc"
  Unit.init(self, args)
end

function Scope:onLoadGraph(channelCount)
  local op = self:addObject("op", libscope.Scope())
  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end
end

function Scope:onLoadViews()
  local view = Class {}
  view:include(ViewControl)

  function view:init(args)
    ViewControl.init(self)
    self:setClassName("Scope.ScopeView")
    local width = args.width
    local graphic = app.Graphic(0, 0, width, 64)
    self:setMainCursorController(graphic)
    self:setControlGraphic(graphic)

    for i = 1, (width // ply) do
      self:addSpotDescriptor{center = (i - 0.5) * ply}
    end

    local outlet = args.outlet
    local scope = app.MiniScope(0, 0, width, 64)
    graphic:addChild(scope)
    scope:watchOutlet(outlet)
  end

  local scopeView = view {
    width = ply,
    outlet = self.objects.op:getOutput("Out L")
  }

  return {
    scope = scopeView
  }, {
    expanded = {"scope"},
    collapsed = {}
  }
end

return Scope
