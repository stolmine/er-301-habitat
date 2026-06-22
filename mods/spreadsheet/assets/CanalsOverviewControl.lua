-- CanalsOverviewControl
--
-- Overview ply for the Canals unit. Main graphic is a placeholder
-- until the routing-state visualization lands (Phase 6 of the
-- 4-input + normalling topology plan). Sub-display hosts three
-- MiniScopes — one per per-block input branch (Low / Centre / High)
-- — each acting as a dive affordance: pressing sub-button 1/2/3
-- calls branch:show() to navigate into the corresponding subchain.
--
-- Modeled on PatchMeter (xroot/Unit/ViewControl/PatchMeter.lua) which
-- uses the same branch:show() mechanic for the chain-dive. Three
-- branches instead of one; sub-buttons instead of the main spot.
--
-- No level / bias controls visible to users — backend GainBias
-- objects pass audio at unity by default (hardSet Gain=1, Bias=0
-- in Canals.lua's onLoadGraph). Any shaping the user wants happens
-- inside the corresponding subchain.

local app = app
local Class = require "Base.Class"
local ViewControl = require "Unit.ViewControl"

local ply = app.SECTION_PLY

local CanalsOverviewControl = Class {}
CanalsOverviewControl:include(ViewControl)

function CanalsOverviewControl:init(args)
  ViewControl.init(self)
  self:setClassName("CanalsOverviewControl")
  local button = args.button or
                     app.logError("%s.init: button is missing.", self)
  self:setInstanceName(button)

  -- Three per-block input branches required.
  self.lowBranch    = args.lowBranch    or app.logError("%s.init: lowBranch missing.", self)
  self.centreBranch = args.centreBranch or app.logError("%s.init: centreBranch missing.", self)
  self.highBranch   = args.highBranch   or app.logError("%s.init: highBranch missing.", self)

  -- Main graphic — single-ply placeholder. Phase 6 will replace
  -- with routing-state visualization (per-block icons showing where
  -- each block is sourced from + AllEnabled indicator).
  local main = app.Graphic(0, 0, ply, 64)
  local label = app.Label(button, 12)
  label:fitToText(3)
  label:setCenter(ply // 2, 32)
  main:addChild(label)
  self:setMainCursorController(main)
  self:setControlGraphic(main)

  self:addSpotDescriptor{
    center = 0.5 * ply,
    radius = ply
  }

  -- Sub-display: 3 MiniScopes side-by-side (one per branch) + 3
  -- sub-button labels. Each MiniScope watches its branch's
  -- monitoring output, so the user sees what audio is flowing into
  -- each per-block subchain. Pressing sub-button 1/2/3 dives into
  -- the corresponding subchain (subReleased handler).
  local sub = app.Graphic(0, 0, 128, 64)

  self.lowScope = app.MiniScope(0, 0, ply, 60)
  self.lowScope:setForegroundColor(app.GRAY5)
  self.lowScope:watchOutlet(self.lowBranch:getMonitoringOutput(1))
  sub:addChild(self.lowScope)

  self.centreScope = app.MiniScope(ply, 0, ply, 60)
  self.centreScope:setForegroundColor(app.GRAY5)
  self.centreScope:watchOutlet(self.centreBranch:getMonitoringOutput(1))
  sub:addChild(self.centreScope)

  self.highScope = app.MiniScope(2 * ply, 0, ply, 60)
  self.highScope:setForegroundColor(app.GRAY5)
  self.highScope:watchOutlet(self.highBranch:getMonitoringOutput(1))
  sub:addChild(self.highScope)

  sub:addChild(app.SubButton("lo",  1))
  sub:addChild(app.SubButton("ctr", 2))
  sub:addChild(app.SubButton("hi",  3))

  -- Direct field assignment per ViewControl convention (Pitch /
  -- GainBias / Gate / OptionControl all do the same). Framework
  -- auto-attaches via addSubGraphic from ViewControl:init.
  self.subGraphic = sub

  -- Re-watch the monitoring outlet if the branch's content changes
  -- (PatchMeter does the same for its patch).
  self.lowBranch:subscribe("contentChanged", self)
  self.centreBranch:subscribe("contentChanged", self)
  self.highBranch:subscribe("contentChanged", self)
end

function CanalsOverviewControl:contentChanged(chain)
  if chain == self.lowBranch and self.lowScope then
    self.lowScope:watchOutlet(chain:getMonitoringOutput(1))
  elseif chain == self.centreBranch and self.centreScope then
    self.centreScope:watchOutlet(chain:getMonitoringOutput(1))
  elseif chain == self.highBranch and self.highScope then
    self.highScope:watchOutlet(chain:getMonitoringOutput(1))
  end
end

function CanalsOverviewControl:subReleased(i, shifted)
  if shifted then return false end
  local branch
  if     i == 1 then branch = self.lowBranch
  elseif i == 2 then branch = self.centreBranch
  elseif i == 3 then branch = self.highBranch
  end
  if branch then
    self:unfocus()
    branch:show()
  end
  return true
end

function CanalsOverviewControl:onRemove()
  if self.lowBranch    then self.lowBranch:unsubscribe("contentChanged", self) end
  if self.centreBranch then self.centreBranch:unsubscribe("contentChanged", self) end
  if self.highBranch   then self.highBranch:unsubscribe("contentChanged", self) end
  ViewControl.onRemove(self)
end

return CanalsOverviewControl
