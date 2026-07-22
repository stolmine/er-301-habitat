-- FadeMuteMeter: a BranchMeter whose Solo/Mute buttons drive a UNIT-LOCAL mute
-- group instead of the chain-level one. The stock BranchMeter routes its sub-
-- button presses via callUp("toggle*OnControl"), which escapes the unit and
-- lands on the parent chain's mute group (so native mixer mute/solo spans the
-- whole chain). Fade Mixer wants mute/solo to act ONLY across its own inputs,
-- so we intercept the buttons here and target a group the unit owns. The audio
-- gating itself (branch:mute/unmute via onMuteStateChanged) is already per-
-- branch, so a unit-local group gives self-contained intra-unit behaviour.
local Class = require "Base.Class"
local BranchMeter = require "Unit.ViewControl.BranchMeter"

local FadeMuteMeter = Class {}
FadeMuteMeter:include(BranchMeter)

function FadeMuteMeter:init(args)
  BranchMeter.init(self, args)
  self:setClassName("biome.FadeMuteMeter")
  -- The unit passes its own MuteGroup instance in via args.muteGroup.
  self.localMuteGroup = args.muteGroup
end

function FadeMuteMeter:subReleased(i, shifted)
  if shifted then
    return false
  end
  if i == 1 then
    local branch = self.branch
    if branch then
      self:unfocus()
      branch:show()
    end
  elseif i == 2 then
    -- Solo, unit-local
    if self.localMuteGroup then self.localMuteGroup:toggleSolo(self) end
  elseif i == 3 then
    -- Mute, unit-local
    if self.localMuteGroup then self.localMuteGroup:toggleMute(self) end
  end
  return true
end

return FadeMuteMeter
