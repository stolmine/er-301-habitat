local app = app
local libscope = require "scope.libscope"
local Class = require "Base.Class"
local Unit = require "Unit"
local ScopeView = require "scope.ScopeView"
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
  local scopeView = ScopeView {
    width = ply,
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

return Scope
