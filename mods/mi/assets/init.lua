local Class = require "Base.Class"
local Library = require "Package.Library"

local MI = Class {}
MI:include(Library)

function MI:init(args)
  Library.init(self, args)
end

return MI
