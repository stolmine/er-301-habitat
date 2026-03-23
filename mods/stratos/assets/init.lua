local Class = require "Base.Class"
local Library = require "Package.Library"

local Stratos = Class {}
Stratos:include(Library)

function Stratos:init(args)
  Library.init(self, args)
end

return Stratos
