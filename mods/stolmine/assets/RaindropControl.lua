local app = app
local libstolmine = require "stolmine.libstolmine"
local Class = require "Base.Class"
local Base = require "Unit.ViewControl.EncoderControl"
local Encoder = require "Encoder"

local ply = app.SECTION_PLY

local RaindropControl = Class {
  type = "RaindropControl",
  canEdit = false,
  canMove = true
}
RaindropControl:include(Base)

function RaindropControl:init(args)
  local delay = args.delay or app.logError("%s.init: delay is missing.", self)

  Base.init(self, "overview")
  self:setClassName("RaindropControl")

  local width = args.width or ply

  local graphic = app.Graphic(0, 0, width, 64)
  self.pDisplay = libstolmine.RaindropGraphic(0, 0, width, 64)
  graphic:addChild(self.pDisplay)
  self:setMainCursorController(self.pDisplay)
  self:setControlGraphic(graphic)

  self:addSpotDescriptor { center = 0.5 * width }

  self.pDisplay:follow(delay)
end

function RaindropControl:setSelectedTap(tap)
  self.pDisplay:setSelectedTap(tap)
end

return RaindropControl
