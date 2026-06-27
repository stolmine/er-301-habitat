-- AnamFieldGate -- the Freeze ply: a Gate whose MAIN graphic is its slice of the
-- all-over "Pond of Recollection" flow-field, with the stock ComparatorView
-- orphaned (hidden). Freeze stays fully controllable and indicated from the
-- comparator SUB-display (scope + fire), so the main is pure viz -- exactly the
-- spreadsheet CompBandControl recipe (replace the fader/main with a custom
-- graphic, orphan the original) applied to a Gate instead of a GainBias.
-- planning/spatial-glitch-impl/07-allover-viz.md

local app = app
local Class = require "Base.Class"
local Gate = require "Unit.ViewControl.Gate"
local libanamnesis = require "anamnesis.libanamnesis"

local ply = app.SECTION_PLY

local AnamFieldGate = Class {}
AnamFieldGate:include(Gate)

function AnamFieldGate:init(args)
  Gate.init(self, args)

  -- Replace the ComparatorView main with this ply's field slice (the
  -- ComparatorView is left orphaned, like CompBandControl orphans the fader).
  local field = libanamnesis.AnamFieldGraphic(0, 0, ply, 64)
  field:follow(args.op)
  field:setCanvas(args.canvasIndex or 0, args.canvasCount or 1)
  field:setFeature(args.feature or 0)

  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(field)

  self.fieldGraphic = field
  self:setMainCursorController(field)
  self:setControlGraphic(container)
end

return AnamFieldGate
