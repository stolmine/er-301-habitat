local Class = require "Base.Class"
local Library = require "Package.Library"

local Grids = Class {}
Grids:include(Library)

function Grids:init(args)
  Library.init(self, args)
end

return Grids
