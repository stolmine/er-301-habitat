-- AnamFieldControl -- a ply whose MAIN graphic is its slice of the all-over
-- "Pond of Recollection" flow-field (planning/spatial-glitch-impl/07-allover-viz.md),
-- while keeping the full AnamSubControl interaction model (tap-shift param subs,
-- sub-button focus, keyboard). It is AnamSubControl with the stock fader swapped
-- for an AnamFieldGraphic (the Helicase custom-main-graphic pattern).
--
-- Each ply passes its canvasIndex (position in the strip) + canvasCount so the
-- graphic renders the correct content-X window; seams align by construction.
-- `feature` selects the per-ply motif (wired live in Phase C).

local app = app
local Class = require "Base.Class"
local AnamSubControl = require "anamnesis.AnamSubControl"
local libanamnesis = require "anamnesis.libanamnesis"

local AnamFieldControl = Class {}
AnamFieldControl:include(AnamSubControl)

function AnamFieldControl:init(args)
  AnamSubControl.init(self, args)

  local field = libanamnesis.AnamFieldGraphic(0, 0, app.SECTION_PLY, 64)
  field:follow(args.op)
  field:setCanvas(args.canvasIndex or 0, args.canvasCount or 1)
  field:setFeature(args.feature or 0)

  local container = app.Graphic(0, 0, app.SECTION_PLY, 64)
  container:addChild(field)

  self.fieldGraphic = field
  self:setMainCursorController(field)
  self:setControlGraphic(container)
end

return AnamFieldControl
