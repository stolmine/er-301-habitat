---
name: subCursorController inheritance from GainBias.init (visual highlight)
description: GainBias.init writes self.bias into self.subCursorController unconditionally and never clears it. Subclasses that swap self.subGraphic to a custom paramSubGraphic have to suppress or redirect that highlight explicitly, or the renderer will draw the highlight at self.bias's screen coordinates (typically sub3 position) through whatever subGraphic is attached.
type: feedback
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---
**Framework mechanics (SDK-verified):**

- `GainBias:init` at `xroot/Unit/ViewControl/GainBias.lua:197` calls `self:setFocusedReadout(self.bias)` unconditionally.
- `GainBias:setFocusedReadout` at `:635-646` stores the readout in `self.focusedReadout` AND calls `self:setSubCursorController(readout)`.
- `Widget:setSubCursorController` at `xroot/Base/Widget.lua:56-66` stores the ref in `self.subCursorController` and, if the widget currently holds encoder focus, triggers `Context:onEncoderFocusChanged`.
- `Context:onEncoderFocusChanged` at `xroot/Base/Context.lua:278-290` reads `focus.subCursorController` (fallback `focus.subGraphic`) and calls `self.subGraphicContext:setCursorController(controller)` on the C++ renderer, which is what actually draws the highlight frame at the readout's screen coords.
- `ViewControl:onCursorEnter/onCursorLeave` at `xroot/Unit/ViewControl/init.lua:130-147` auto-add/remove `self.subGraphic` but never touch `subCursorController`.

**Net:** `subCursorController` persists pointing at `self.bias` for the widget's lifetime unless the subclass changes it. `self.bias` lives in the stock `normalSubGraphic` (col3 / sub3 position). When `setParamMode(true)` swaps `self.subGraphic` to `paramSubGraphic`, the highlight tries to render at `self.bias`'s screen coords -- which now visually overlap the sub3 button of whatever subGraphic is attached. Spurious sub3 highlight is the symptom.

**Fix pattern (Decision 8 of the shift audit, applied in commits 9a821e8 + d74af56):**

```lua
function C:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
  if self.paramMode then
    self:setSubCursorController(self.paramModeDefaultSub)
    -- paramModeDefaultSub: a sub-readout to highlight, or nil to clear.
  end
end
```

**Key invariants:**

- Encoder routing is independent of `subCursorController` -- it's governed by `focusedReadout` / `paramFocusedReadout` at `GainBias.lua:786-793`. Clearing the cursor controller does NOT break the encoder.
- Don't assign `self.focusedReadout` directly; heap-corruption class (see `feedback_gainbias_dual_mode_focus.md`). Use `self:setFocusedReadout(...)`.
- `self.subCursorController = nil` direct assignment also works (it's not GainBias-internal) but prefer `self:setSubCursorController(nil)` so the onEncoderFocusChanged notification fires and the renderer updates live.

**Known unresolved (pinned 2026-04-22):** on five controls that declare `paramModeDefaultSub` to point at a bias-bound sub-readout, the highlight still does not render visibly on paramMode entry even though the assignment fires. Candidate causes tracked in `todo.md` Shift audit follow-up: Readout-vs-SubButton highlight rendering difference, main-cursor distraction, downstream override, or paramSubGraphic tree visibility at dispatch time. Spurious sub3 bleed is confirmed cleared; it's the positive default highlight that doesn't land. Do not layer more paramMode `setSubCursorController` calls without re-investigating at the renderer level.

**Authoring checklist when adding a new GainBias paramMode control:**

1. Save `self.normalSubGraphic = self.subGraphic` before building paramSubGraphic.
2. Build paramSubGraphic separately; add children including sub buttons/readouts.
3. Set `self.paramMode = <false|true>` per grandfather intent.
4. If sub1 (or any sub) in paramSubGraphic shares the Bias parameter with `self.bias`, set `self.paramModeDefaultSub = self.<thatReadout>`. Otherwise leave unset.
5. Provide the canonical `onCursorEnter` / `onCursorLeave` per `feedback_parammode_convention.md`.
