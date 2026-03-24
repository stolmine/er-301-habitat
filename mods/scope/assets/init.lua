local Class = require "Base.Class"
local Library = require "Package.Library"

local Scope = Class {}
Scope:include(Library)

function Scope:init(args)
  Library.init(self, args)
end

return Scope
