local app = app
local libscope = require "scope.libscope"
local Class = require "Base.Class"
local Unit = require "Unit"
local ViewControl = require "Unit.ViewControl"
local ply = app.SECTION_PLY

local ScopeWide = Class {}
ScopeWide:include(Unit)

function ScopeWide:init(args)
  args.title = "Scope 2x"
  args.mnemonic = "S2"
  Unit.init(self, args)
end

function ScopeWide:onLoadGraph(channelCount)
  local op = self:addObject("op", libscope.Scope())
  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
end

function ScopeWide:onLoadViews()
  local view = Class {}
  view:include(ViewControl)

  function view:init(args)
    ViewControl.init(self)
    self:setClassName("Scope.ScopeWideView")
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
    width = 2 * ply,
    outlet = self.objects.op:getOutput("Out L")
  }

  return {
    scope = scopeView
  }, {
    expanded = {"scope"},
    collapsed = {}
  }
end

return ScopeWide
