-- Network gltch overview ply: replaces the bar fader with the
-- 3D phase-space viz. Encoder still drives the underlying Glitch
-- parameter (standard GainBias.encoder). Pattern follows
-- LaretOverviewControl.lua.

local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"

local ply = app.SECTION_PLY

local NetworkOverviewControl = Class {}
NetworkOverviewControl:include(GainBias)

function NetworkOverviewControl:init(args)
  GainBias.init(self, args)

  -- Replace the standard bar fader display with the 3D viz.
  -- args.op is the Network DSP object.
  local overview = libspreadsheet.NetworkOverviewGraphic(0, 0, ply, 64)
  overview:follow(args.op)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(overview)
  self:setMainCursorController(overview)
  self:setControlGraphic(container)
end

return NetworkOverviewControl
