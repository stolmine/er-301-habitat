local Class = require "Base.Class"
local Library = require "Package.Library"

local Rings = Class {}
Rings:include(Library)

function Rings:init(args)
  Library.init(self, args)
end

return Rings
