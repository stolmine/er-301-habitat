local Class = require "Base.Class"
local Library = require "Package.Library"

local Marbles = Class {}
Marbles:include(Library)

function Marbles:init(args)
  Library.init(self, args)
end

return Marbles
