local Class = require "Base.Class"
local Library = require "Package.Library"

local Commotio = Class {}
Commotio:include(Library)

function Commotio:init(args)
  Library.init(self, args)
end

return Commotio
