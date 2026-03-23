local Class = require "Base.Class"
local Library = require "Package.Library"

local Peaks = Class {}
Peaks:include(Library)

function Peaks:init(args)
  Library.init(self, args)
end

return Peaks
