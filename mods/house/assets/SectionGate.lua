-- SectionGate -- one section of a multi-section unit: a headline
-- parameter on the encoder, that section's full parameter board on
-- ENTER, and a true bypass on SHIFT.
--
-- Reusable by design. Nothing in the tree did this before, and any
-- future multi-section unit gets it for free.
--
-- THE GESTURE ASSIGNMENT IS NOT ARBITRARY. The original request was
-- "top level all gates, parameters on shift", but the two must swap,
-- because the house already spends both:
--   ENTER -> parameter board. This is habitat's established idiom for
--            section-to-its-parameters (Impasto's per-band expansions,
--            Parfait's eight-per-band, Breccia, Pecto, Vitrail).
--   SHIFT -> bypass. Shift is otherwise the param-mode sub-display
--            toggle (DensityControl, MixControl, LaretsMixControl), and
--            THIS CONTROL DELIBERATELY DOES NOT USE THAT PATTERN, so
--            there is no collision. The cost is that a SectionGate
--            cannot also carry shift sub-readouts; all parameters live
--            in the expansion instead.
-- The encoder keeps driving the headline parameter, so the top row
-- stays playable rather than being a row of switches.
--
-- BYPASS IS VISIBLE ON THE PLY ITSELF, not only in the sub display,
-- because the whole point is reading the strip's state across sections
-- at a glance.
--
-- Design: planning/strata-channel-strip.md, "UI: SETTLED".

local app = app
local Class = require "Base.Class"
local GainBias = require "Unit.ViewControl.GainBias"

local ply = app.SECTION_PLY

local SectionGate = Class {}
SectionGate:include(GainBias)

-- args, beyond GainBias's own:
--   sectionName  short label for the ply, kept SHORT: a 42 px ply at
--                font 10 holds roughly seven glyphs
--   engageOption od::Option, 1 = engaged, 2 = bypassed
function SectionGate:init(args)
  GainBias.init(self, args)
  self:setClassName("SectionGate")

  self.sectionName = args.sectionName or args.button or "sect"
  self.engageOption = args.engageOption or
                          app.logError("%s.init: engageOption is missing.", self)
  self.engageOption:enableSerialization()

  self.shiftHeld = false
  self.shiftUsed = false

  -- Wrap the fader GainBias already built, rather than replacing it, so
  -- the encoder, range and readout behaviour all stay standard.
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(self.fader)
  self.bypassIndicator = app.BinaryIndicator(0, 0, ply, 8)
  container:addChild(self.bypassIndicator)
  self:setControlGraphic(container)
  -- The fader remains the cursor controller: the encoder must still
  -- drive the headline parameter.
  self:setMainCursorController(self.fader)

  self:updateBypass()
end

function SectionGate:isBypassed()
  return self.engageOption:value() == 2
end

-- Two cues, deliberately. The indicator is the at-a-glance one; the
-- label carries it too so the state survives if the indicator is hard
-- to read against a busy fader.
function SectionGate:updateBypass()
  if self:isBypassed() then
    self.bypassIndicator:off()
    self.fader:setLabel(self.sectionName .. "-")
  else
    self.bypassIndicator:on()
    self.fader:setLabel(self.sectionName)
  end
end

function SectionGate:toggleBypass()
  self.engageOption:set(self:isBypassed() and 1 or 2)
  self:updateBypass()
end

function SectionGate:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  self:updateBypass()
end

function SectionGate:onCursorLeave(spot)
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function SectionGate:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  return true
end

-- Only a shift that was NOT used as a modifier toggles the bypass. A
-- shift-turn of the encoder is fine-adjust and must not also flip the
-- section off, which is the same guard every paramMode control uses.
function SectionGate:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    self:toggleBypass()
  end
  self.shiftHeld = false
  return true
end

function SectionGate:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  return GainBias.encoder(self, change, shifted)
end

return SectionGate
