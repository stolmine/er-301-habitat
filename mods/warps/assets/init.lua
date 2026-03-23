local Class = require "Base.Class"
local Library = require "Package.Library"

local Warps = Class {}
Warps:include(Library)

function Warps:init(args)
  Library.init(self, args)
end

return Warps
