local Class = require "Base.Class"
local Library = require "Package.Library"

local NR = Class {}
NR:include(Library)

function NR:init(args)
  Library.init(self, args)
end

return NR
