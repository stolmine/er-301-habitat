-- VitrailClockControl
--
-- Clock Src selector with the tunnel visualization stacked on top of the
-- fader, following the MirrorOverviewControl idiom: the custom graphic
-- becomes the control graphic and takes the main cursor, while the encoder
-- still edits the underlying mode. Clock Src is the right host because the
-- tunnel's rotation IS the A/B clock drift beat, so the picture and the
-- control are describing the same thing.
--
-- The mode name would normally live on the fader we just displaced, so it is
-- re-drawn as a label over the tunnel. Text is OR-blended (lighten-only), so
-- white on the dark tube reads cleanly with no clear() dance
-- (feedback_framebuffer_blend_vs_set).

local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local ModeSelector = require "spreadsheet.ModeSelector"

local ply = app.SECTION_PLY

local VitrailClockControl = Class {}
VitrailClockControl:include(ModeSelector)

function VitrailClockControl:init(args)
  ModeSelector.init(self, args)

  local tunnel = libspreadsheet.VitrailTunnelGraphic(0, 0, ply, 64)
  tunnel:follow(args.op)

  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(tunnel)

  -- Added after the tunnel so it composites on top.
  self.modeLabel = app.Label("", 10)
  self.modeLabel:setCenter(math.floor(ply / 2), 8)
  container:addChild(self.modeLabel)

  self:setMainCursorController(tunnel)
  self:setControlGraphic(container)

  self:updateLabel()
end

-- ModeSelector drives the displaced fader's label; mirror it onto the overlay.
function VitrailClockControl:updateLabel()
  ModeSelector.updateLabel(self)
  if self.modeLabel then
    local name = self.modeNames[self:currentIndex()]
    if name then
      self.modeLabel:setText(name)
    end
  end
end

return VitrailClockControl
