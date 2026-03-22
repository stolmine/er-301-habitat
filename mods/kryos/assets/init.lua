local Class = require "Base.Class"
local Library = require "Package.Library"

local Kryos = Class {}
Kryos:include(Library)

function Kryos:init(args)
  Library.init(self, args)
end

return Kryos
