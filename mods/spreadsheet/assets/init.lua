local Class = require "Base.Class"
local Library = require "Package.Library"

local Spreadsheet = Class {}
Spreadsheet:include(Library)

function Spreadsheet:init(args)
  Library.init(self, args)
end

return Spreadsheet
