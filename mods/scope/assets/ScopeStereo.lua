local app = app
local libscope = require "scope.libscope"
local Class = require "Base.Class"
local Unit = require "Unit"
local ScopeView = require "scope.ScopeView"
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
  local op = self.objects.op
  local args = { width = 2 * ply }
  if self.channelCount > 1 then
    args.outlets = { op:getOutput("Out L"), op:getOutput("Out R") }
  else
    args.outlet = op:getOutput("Out L")
  end
  local scopeView = ScopeView(args)
  self.scopeView = scopeView
  return {
    scope = scopeView
  }, {
    expanded = {"scope"},
    collapsed = {}
  }
end

return ScopeStereo
