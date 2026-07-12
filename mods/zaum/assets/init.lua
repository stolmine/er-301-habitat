local Class = require "Base.Class"
local Library = require "Package.Library"

local Zaum = Class {}
Zaum:include(Library)

function Zaum:init(args)
  Library.init(self, args)
end

return Zaum
