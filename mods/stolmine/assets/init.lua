local Class = require "Base.Class"
local Library = require "Package.Library"

local Stolmine = Class {}
Stolmine:include(Library)

function Stolmine:init(args)
  Library.init(self, args)
end

return Stolmine
