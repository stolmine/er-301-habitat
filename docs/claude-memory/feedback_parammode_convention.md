---
name: Spreadsheet paramMode UI convention
description: Canonical shape + locked decisions for GainBias (Pattern A) and EncoderControl (Pattern C) custom controls that toggle between a stock sub-display and a custom "paramSubGraphic". Applies to the entire spreadsheet package + Pecto/DensityControl. Full spec at planning/shift-handling.md; this memory is the quick reference.
type: feedback
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---
When authoring a new spreadsheet-package control that swaps sub-displays on shift-toggle, follow this shape. Decisions 1-8 are locked (2026-04-21, see `planning/shift-handling.md`); Decision 8 has an unresolved visible-highlight wrinkle pinned 2026-04-22.

## Flag and field names

- `self.paramMode` boolean (false = stock GainBias sub, true = custom). BandControl is the sole 3-mode cycle (0/1/2). NEVER use alternate names (`compMode`, `focusMode`, `mathMode`, etc.); that was normalized.
- `self.normalSubGraphic` = stock GainBias subGraphic captured at init.
- `self.paramSubGraphic` = custom subGraphic you build.
- `self.paramFocusedReadout` = which readout inside paramSubGraphic the encoder targets when paramMode is active. Separate from GainBias's `self.focusedReadout`. Nil means "no sub focused, encoder falls through to `self.bias`".
- `self.paramModeDefaultSub` (optional, Decision 8) = a sub-readout to highlight on paramMode entry. Set only on controls where sub1's Readout shares the underlying Bias parameter with `self.bias` (current set: HelicaseOverview/Mod/Shaping/LaretOverview mix|ratio|index|skew, MixInput level). Other controls leave it unset.

## onCursorEnter / onCursorLeave persistence (Decision 7)

`paramMode` persists across cursor leave/return within a session. NOT serialized across quicksave. `paramFocusedReadout` cleared on leave so the user must deliberately tap to focus.

```lua
function C:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    self:setSubCursorController(self.paramModeDefaultSub)  -- Decision 8
  end
end

function C:onCursorLeave(spot)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end
```

For BandControl's 3-mode variant, guard is `if self.paramMode ~= 0 then`.

## Shift mechanics (Decisions 1, 2, 5)

- `shiftPressed` captures `shiftHeld=true`, `shiftUsed=false`, and a `shiftSnapshot` of the focused readout's current value.
- `encoder` sets `shiftUsed=true` on any encoder touch during shift-hold. That suppresses the mode-toggle on release (Decision 1).
- `shiftReleased` toggles paramMode only if `shiftHeld and not shiftUsed` AND (no focused readout OR snapshot unchanged).
- `spotReleased` in Pattern C drops the shift+spot secondary toggle path (Decision 2). Shift+spot is reserved for stock semantics.
- `subReleased` opens `Keyboard.Decimal` when shifted in paramMode via `spreadsheet.ShiftHelpers.openKeyboardFor(readout, label)` (Decision 5). Pattern C skips Decision 5 since its sub-buttons are discrete toggles.

## setParamMode swap mechanics (Decision 3)

Both Pattern A and Pattern C use `removeSubGraphic(old) / addSubGraphic(new)` to swap. No more show()/hide() on individual children. Build two separate subGraphics at init and swap the ref.

## Shared helper

- `mods/spreadsheet/assets/ShiftHelpers.lua` exposes `M.openKeyboardFor(readout, label)` modeled on `GainBias:doGainSet`/`doBiasSet`.
- Require as `require "spreadsheet.ShiftHelpers"` from any spreadsheet or biome asset.

## Known pinned issue (Decision 8, 2026-04-22)

Five controls (bias-bound sub1) declare `paramModeDefaultSub` so the sub cursor controller points at the bias-reflecting sub on paramMode entry. Encoder edits bias, the sub's readout visually ticks, sub highlight *should* render there. In practice the spurious sub3 highlight is fixed but the default highlight does not render visibly. Tracked in todo.md. Do not layer more fixes on top without re-investigating root cause at the renderer level.

## Grandfather list (initial paramMode state)

Most controls init `paramMode = false` (stock sub is default). Five init `paramMode = true` because their custom view IS the headline UI: HelicaseModControl, HelicaseOverviewControl, HelicaseShapingControl, LaretOverviewControl, CompBandControl, RatchetControl. Preserve this on any future Decision 7 rethink -- the user explicitly grandfathered these.

## Heap-corruption invariant (cross-reference)

Never assign `self.focusedReadout` directly; always `self:setFocusedReadout(...)`. See `feedback_gainbias_dual_mode_focus.md`. `self.paramFocusedReadout` is separate and safe to null.
