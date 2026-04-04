local Class = require "Base.Class"
local Library = require "Package.Library"

local Biome = Class {}
Biome:include(Library)

function Biome:init(args)
  Library.init(self, args)
end

return Biome
