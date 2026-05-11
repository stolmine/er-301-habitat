---
name: GainBias-subclass dual-mode controls must pin focusedReadout at all times
description: When a custom control inherits Unit.ViewControl.GainBias and adds a shift-toggle between a params sub-display and the default, self.focusedReadout must stay pointing at a live Readout in every branch — GainBias's own onFocused/cancel/zero dereference it, and a nil value crashes (the SDK's xpcall catches it and shows as a frozen crash dialog).
type: feedback
originSessionId: 6486de73-452f-466c-9c43-03152c66fce2
---
## Pattern

Many spreadsheet controls inherit `GainBias` and implement a shift-toggle between a "params" sub-display (custom readouts + sub-buttons to focus each) and the default/level sub-display. References: `MixInputControl` (Blanda), `HelicaseShapingControl`, `CompBandControl`, `FocusShapeControl` (Blanda), `DriveControl` (Parfait).

## The rule

`self.focusedReadout` is an attribute that **GainBias itself owns and dereferences** in `onFocused` / `cancelReleased` / `zeroPressed`. It is NOT the same as any `paramFocusedReadout` / `compFocusedReadout` / `shapeFocusedReadout` that your own subclass uses to route encoder events inside its custom param mode.

Every code path in a dual-mode subclass must leave `self.focusedReadout` pointing at a live Readout. For level-editing controls that means `self.bias` is a safe universal default.

## Use `self:setFocusedReadout(self.bias)`, NOT `self.focusedReadout = self.bias`

Direct field assignment is **not equivalent** to calling the method. `GainBias:setFocusedReadout(readout)` (see `xroot/Unit/ViewControl/GainBias.lua:635`) also:
- calls `readout:save()`
- calls `self:setSubCursorController(readout)` — installs the cursor routing
- calls `Encoder.set(...)` to refresh the encoder state (coarse vs fine)

Skipping these (via direct assignment) leaves the ViewControl's sub cursor controller nil and the encoder state stale after every shift-toggle round-trip. On Blanda (ScanSkewControl, 2026-04-15) this caused **heap corruption** surfacing as `malloc(): invalid size` aborts on unit delete, quicksave-menu entry, and quicksave load — the destroyed control left the ViewControl base in a half-initialized cursor-routing state, and destruction walked something stale.

Replacing direct `self.focusedReadout = self.bias` with `self:setFocusedReadout(self.bias)` in init, both branches of the mode-swap method, and the onCursorLeave reset path cleared the crash completely.

**Rule**: in dual-mode subclasses, always drive `focusedReadout` through the method. Direct assignment is a code smell.

## Places you must set `self.focusedReadout = self.bias`

1. **End of init**, after whichever `setXMode(true)` starts the control. GainBias.init sets it to `self.bias` at line 197, but if your custom init then calls `setParamMode(true)` in a way that does not re-set it (the common pattern — the enabled=false branch is the one that calls `setFocusedReadout(self.bias)`, and you skipped that branch), the pointer is effectively left in an ambiguous state. Set explicitly.

2. **Inside `setXMode(enabled)`, on every branch.** Do not only set it in the level-mode branch. Set it in the params branch too. The param-mode "focused readout" for encoder routing lives in your own field (paramFocusedReadout) — GainBias's focusedReadout stays separate, always pointing at `self.bias`.

3. **Inside `onCursorLeave`** if you reset `mixMode`/`paramMode`/`compMode` back to the default there. That path is taken when the user navigates away from a ply while it was in the shifted sub-mode. If you flip the mode flag but do not restore `focusedReadout`, the next time the user enters this same ply, onFocused will see stale nil.

## Symptom signature

Lua runtime error:

```
Unit.ViewControl.GainBias.onFocused
  attempt to index a nil value (field 'focusedReadout')
```

Stack goes through `GainBias.spotReleased -> ViewControl.spotReleased -> focus -> onFocused`. Caught by the SDK's top-level xpcall so it surfaces as a crash dialog — which can look like a "hang" if the dialog hasn't rendered yet.

A gdb `thread apply all bt` on the emu will show the `interp` thread stuck inside `Events_wait` after `luaG_runerror` / `luaG_typeerror` / `luaV_finishget` — that's the crash-reporter dialog blocking for user ack, not an infinite loop.

## Why this bites

The idiom of keeping two separate "focused readout" pointers (one for your param mode, one inherited from GainBias) is useful — it lets you route encoder/sub-button events through your own fields without interfering with GainBias defaults. But the inherited pointer is NOT purely vestigial: GainBias still dereferences it in its own methods that you haven't overridden (most notably `onFocused`, which the ViewControl base calls from `focus()`, called from `spotReleased`).

## Quick audit rule when adding a new dual-mode GainBias subclass

Grep your control for every place `self.focusedReadout` or `self:setFocusedReadout(...)` appears. Also check every `setXMode` / `onCursorLeave` / `onRemove` / `init` path. Make sure `self.focusedReadout` is **non-nil at exit of every one of them**. The cheapest and correct default is always `self.bias`.

## Reference commits

- `eac38aa` — initial defensive fix at init for `HelicaseShapingControl`, `CompBandControl`, `FocusShapeControl`.
- `ca98128` — `MixInputControl` extension: also maintain it inside `setMixMode(false)` and `onCursorLeave`.
- (2026-04-15, Blanda) — `ScanSkewControl` heap-corruption-on-delete resolved by switching direct `self.focusedReadout = self.bias` to the `self:setFocusedReadout(self.bias)` method calls. Symptom was `malloc(): invalid size (unsorted)` aborting inside a *fresh* `Blanda::Blanda()` on reboot-from-quicksave — a classic delayed-heap-corruption surface from the earlier destruction path.
