local app = app
local libscope = require "scope.libscope"
local Class = require "Base.Class"
local Unit = require "Unit"
local ScopeView = require "scope.ScopeView"
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
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end
end

function ScopeWide:onLoadViews()
  local scopeView = ScopeView {
    width = 2 * ply,
    outlet = self.objects.op:getOutput("Out L")
  }
  self.scopeView = scopeView
  return {
    scope = scopeView
  }, {
    expanded = {"scope"},
    collapsed = {}
  }
end

return ScopeWide
