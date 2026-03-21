local Class = require "Base.Class"
local Library = require "Package.Library"

local Plaits = Class {}
Plaits:include(Library)

function Plaits:init(args)
  Library.init(self, args)
end

return Plaits
