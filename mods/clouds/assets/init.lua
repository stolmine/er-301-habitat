local Class = require "Base.Class"
local Library = require "Package.Library"

local Clouds = Class {}
Clouds:include(Library)

function Clouds:init(args)
  Library.init(self, args)
end

return Clouds
