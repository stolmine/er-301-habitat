# Unified Shift-Handling Convention

Audit of how the SHIFT button is handled across spreadsheet + Pecto controls,
with a proposed unification spec and decision matrix. All claims here were
verified by reading the source of every control listed; do not trust any
unverified summaries that may pre-date this doc.

## Scope

Custom ViewControl Lua files in:
- `mods/spreadsheet/assets/`
- `mods/biome/assets/Pecto.lua` and the controls it instantiates
  (`DensityControl`, plus imports from spreadsheet)

The ER-301 SDK lives at `~/repos/er-301-stolmine/xroot`. The relevant base
classes are:
- `xroot/Unit/ViewControl/init.lua` (ViewControl)
- `xroot/Unit/ViewControl/EncoderControl.lua`
- `xroot/Unit/ViewControl/GainBias.lua`
- `xroot/Application.lua` -- dispatches `EVENT_PRESS_SHIFT` /
  `EVENT_RELEASE_SHIFT` as `notify("shiftPressed" / "shiftReleased")` to
  whichever widget called `Widget:grabFocus("shiftPressed",
  "shiftReleased")`. `grabFocus` / `releaseFocus` is the canonical opt-in
  for receiving shift events.

GainBias.init at line 197 calls `self:setFocusedReadout(self.bias)`, so
`focusedReadout` is always non-nil immediately after construction.

## Patterns

Three distinct patterns exist. Two of them (A, C) drive a sub-display
toggle on shift; the third (B) does not.

### Pattern A: GainBias dual-mode shift toggle

`paramMode` boolean drives swap between `normalSubGraphic` (the GainBias
default) and `paramSubGraphic` (custom). Shift toggles paramMode; sub-buttons
in paramMode focus a `paramFocusedReadout`; encoder routes there.

Confirmed conformant (14):
DriveControl, FeedbackControl, MixControl, RauschenCutoffControl,
TimeControl, ParfaitMixControl, CompMixControl, CompBandControl,
LaretsMixControl, LaretOverviewControl, HelicaseModControl,
HelicaseOverviewControl, HelicaseShapingControl, DensityControl.

3-mode variant (cycles 0/1/2): BandControl.

Inverted-flag variants (`mode=true` means default GainBias, not custom):
FocusShapeControl (`focusMode`), ScanSkewControl (`scanMode`),
MixInputControl (`mixMode`).

Canonical mechanics:
- `onCursorEnter`: super's `onCursorEnter` then `grabFocus("shiftPressed",
  "shiftReleased")`.
- `onCursorLeave`: reset to normal mode if currently in paramMode, then
  `releaseFocus(...)` then super's `onCursorLeave`.
- `shiftPressed`: `shiftHeld=true`, `shiftUsed=false`, snapshot the focused
  param-mode readout's value (or nil).
- `shiftReleased`: toggle iff `shiftHeld and not shiftUsed and snapshot
  unchanged`.
- `encoder(change, shifted)`: `if shifted and shiftHeld then shiftUsed=true
  end`; route to paramFocusedReadout if in paramMode, else to
  `GainBias.encoder`.
- `subReleased(i, shifted)`: `if shifted then return false end`; in paramMode
  focus a readout; else delegate to `GainBias.subReleased`.
- `spotReleased`: in paramMode, clear focus + reset to normal, then delegate.
- `zeroPressed` / `cancelReleased`: route to paramFocusedReadout if set, else
  delegate.

### Pattern B: EncoderControl static + focusedReadout, no shift toggle

Single `subGraphic`, no shift handlers, no `grabFocus("shiftPressed",...)`.
Sub-buttons focus a `focusedReadout`. Some use shift+encoder to repurpose the
encoder turn (BandListControl/ChaselightControl scroll the list when
shifted; DelayInfoControl just passes shifted through).

Confirmed conformant (5):
BandListControl, ChaselightControl, DelayInfoControl,
HelicaseSyncControl, LaretClockControl.

These controls do not need shift toggles (no second submenu to switch to).
They are not bugs; they are a different problem.

### Pattern C: EncoderControl + asymmetric shift + show/hide

Single big `subGraphic` with both modes' children always present; mode
toggle hides/shows children rather than swapping subGraphic refs.

Confirmed (2): TransformGateControl, RatchetControl.

Mechanics that diverge from Pattern A:
- `shiftPressed`: if a readout is focused, defer (capture snapshot); if
  none focused, **toggle immediately on press**.
- `shiftReleased`: if deferred, toggle iff snapshot unchanged.
- No `shiftUsed` tracking on encoder; only the snapshot value-compare guards
  the toggle. Net: turning the encoder during shift hold then back to the
  original value still fires the toggle.
- `spotReleased(spot, shifted)`: when `shifted`, **secondary toggle path**.
- `setMode(enabled)` uses `show()/hide()` on every child rather than
  swapping subGraphic.

## Verified divergences and hazards

1. **Mode-flag polarity inversion** (FocusShape, ScanSkew, MixInput) --
   semantically identical, just confusing to read next to the other 14.
2. **Pattern C has two toggle paths** (shift release + shift+spot); Pattern
   A has one. The shift+spot path is undocumented and non-obvious.
3. **Pattern C's encoder doesn't suppress the toggle.** A nudge-and-back on
   a focused readout fires the toggle anyway. Pattern A blocks any encoder
   touch during shift hold.
4. **GainBias's stock shift+sub2/sub3 = keyboard-set semantic is suppressed
   in paramMode.** Pattern A returns false on `shifted` in subReleased, so
   shift+sub does nothing in paramMode. In normal mode it falls through to
   GainBias and works. Inconsistent.
5. **`cancelReleased` shift-sensitivity.** GainBias base returns false on
   `shifted`. Pattern A overrides drop the shift check in paramMode (always
   handles), keep it in normal mode. Pattern C unconditionally handles.
6. **Defensive `self.focusedReadout = self.bias` in init is redundant.**
   GainBias.init already does it. The crashes documented in
   `feedback_gainbias_dual_mode_focus.md` came from `setParamMode(true)`
   paths that clear `paramFocusedReadout` but not `focusedReadout`; since
   GainBias.init had set it, the defensive lines don't change behavior.
   Leave existing ones in place; do not propagate to new controls.
7. **Pecto already mixes patterns.** It uses MixControl (A),
   TransformGateControl (C), DensityControl (A) directly. Its other plies
   (size, feedback, inputLevel, outputLevel, tanhAmt, tune) are stock
   GainBias / Pitch -- they get shift+sub2/sub3 keyboard semantics only.

## Decision matrix with UI walkthroughs

Each row is a button sequence. "C" / "A" columns describe what the user
sees today; "Proposed" describes what they would see after the change.

### Decision 1 -- Encoder-suppression (A) vs snapshot-only (C)

Test control: RatchetControl (C today) -> A.

| # | Action | Current (C) | Proposed (A) |
|---|---|---|---|
| 1.1 | hold shift, release | toggle | toggle |
| 1.2 | hold shift, encoder +3, release | blocked (snapshot differs) | blocked (shiftUsed) |
| 1.3 | hold shift, encoder +3, encoder -3, release | **toggles** (snapshot equal) | blocked |
| 1.4 | hold shift while CV moves param, release | blocked | blocked |
| 1.5 | hold shift with no readout focused, encoder +3, release | toggles (no snapshot) | blocked |

The footgun is 1.3 + 1.5. Choosing A makes "I touched the encoder" mean "I
did not intend to flip the mode," which is a more conservative default.

### Decision 2 -- Drop Pattern C's shift+spot secondary toggle

Test control: TransformGateControl in Excel's xform ply.

| # | Action | Current | Proposed |
|---|---|---|---|
| 2.1 | tap shift | toggle | toggle |
| 2.2 | hold shift, tap M1 (ply spot) | toggle + spot release | spot release only |
| 2.3 | hold shift, navigate cursor away then back | toggle on shift release | (same) |

Cost: power users lose the "tap-with-shift-held" shortcut. Benefit: shift +
spot means the same thing on every control.

### Decision 3 -- show/hide vs subGraphic-swap

Test control: RatchetControl.

| # | Action | Current (show/hide) | Proposed (swap) |
|---|---|---|---|
| 3.1 | toggle gate <-> ratchet repeatedly | works | works |
| 3.2 | CPU on hidden side | hidden refs may still tick | refs detached |
| 3.3 | cursor leaves ply during paramMode | reset; relies on per-child hide | reset; addSubGraphic wipes everything |
| 3.4 | save/load mid-toggle | safe | safe |

Probably no observable UI diff. Internal: removes a class of "did I forget
to hide this child" bugs. Cost: ~30 lines of refactor per Pattern C control.

### Decision 4 -- Normalize the inverted mode flag

| # | Code site | Current | Proposed |
|---|---|---|---|
| 4.1 | "enter custom submenu" | `setFocusMode(false)` | `setParamMode(true)` |
| 4.2 | onCursorLeave reset | `if not self.focusMode then ...; self.focusMode = true end` | `if self.paramMode then ...; self.paramMode = false end` |

Zero UI diff. Pure code-clarity change. Affects FocusShapeControl,
ScanSkewControl, MixInputControl.

### Decision 5 -- shift+sub in paramMode = open keyboard

Test control: DriveControl, paramMode active, encoder focused on "tone"
(sub1).

| # | Action | Current | Option A | Option B |
|---|---|---|---|---|
| 5.1 | tap sub1 (already focused) | nothing | "Enter tone" kb | nothing |
| 5.2 | hold shift, tap sub1 | nothing | nothing | "Enter tone" kb |
| 5.3 | hold shift, tap sub2 (freq, not focused) | nothing | "Enter freq" kb | "Enter freq" kb |
| 5.4 | normal mode, hold shift, tap sub2 (= gain) | "Enter gain" kb (GainBias default) | (same) | (same) |

Option A = double-tap-focused-sub opens kb. Matches BandListControl /
ChaselightControl idiom but conflicts with future double-tap gestures.

Option B = shift+sub opens kb for that readout. Matches stock GainBias
shift+sub2/sub3. Recommended for consistency.

### Decision 6 -- Pecto migration

Three options:

**(a) Leave alone.** Pecto stays in biome with current pattern mix.
Front-panel UX unchanged.

**(b) Move package to spreadsheet, plies stock.** Same UI as (a). Reason:
live in the same package as the convention's authoritative consumers so
future spec changes propagate.

**(c) Move package + convert plies to Pattern A custom controls.** Each
plain-GainBias ply gets a paramSubGraphic exposing buried params. Front
panel surface area expands without taking more plies.

Plausible mappings to make (c) concrete (these are guesses; design pass
needed):

| Ply | normal-mode fader | shift submenu candidates |
|---|---|---|
| size | comb size | drift, slope, ? |
| feedback | feedback amount | tone, DC-blocker freq, sat amt |
| inputLevel | input gain | pre-saturate amt, DC trim |
| outputLevel | output gain | pan, mute, dry/wet |
| tanhAmt | sat amount | pre-tilt, post-tilt, mode |

(c) is design work, not refactoring. Defer until you've used Pecto enough
to know what additional surfacing is actually wanted.

## Recommended unified spec (synthesis of decisions)

If decisions 1, 2, 3, 4 land as proposed, the canonical Pattern A becomes:

```
self.paramMode = false               -- false = stock GainBias submenu
self.normalSubGraphic = self.subGraphic
self.paramSubGraphic = app.Graphic(0, 0, 128, 64)
-- ... build paramSubGraphic ...

function C:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)
  self.paramMode = enabled
  self.paramFocusedReadout = nil
  self:setSubCursorController(nil)
  if enabled then
    self.subGraphic = self.paramSubGraphic
  else
    self.subGraphic = self.normalSubGraphic
    self:setFocusedReadout(self.bias)
  end
  self:addSubGraphic(self.subGraphic)
end

function C:onCursorEnter(spot)
  GainBias.onCursorEnter(self, spot)
  self:grabFocus("shiftPressed", "shiftReleased")
end

function C:onCursorLeave(spot)
  if self.paramMode then
    self:removeSubGraphic(self.subGraphic)
    self.paramMode = false
    self.subGraphic = self.normalSubGraphic
    -- addSubGraphic deferred to next onCursorEnter? Verify framework.
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function C:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function C:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then
    if self.paramFocusedReadout and self.shiftSnapshot then
      local cur = self.paramFocusedReadout:getValueInUnits()
      if cur ~= self.shiftSnapshot then
        self.shiftHeld = false
        self.shiftSnapshot = nil
        return true
      end
    end
    self:setParamMode(not self.paramMode)
  end
  self.shiftHeld = false
  self.shiftSnapshot = nil
  return true
end

function C:encoder(change, shifted)
  if shifted and self.shiftHeld then self.shiftUsed = true end
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:encoder(change, shifted,
      self.encoderState == Encoder.Fine)
    return true
  end
  return GainBias.encoder(self, change, shifted)
end

function C:subReleased(i, shifted)
  if self.paramMode then
    if shifted then
      -- Decision 5 option B: shift+sub opens keyboard for that readout
      local r = self:_paramReadoutForButton(i)
      if r then self:_openKeyboardFor(r) end
      return true
    end
    local r = self:_paramReadoutForButton(i)
    if r then
      r:save()
      self.paramFocusedReadout = r
      self:setSubCursorController(r)
      if not self:hasFocus("encoder") then self:focus() end
    end
    return true
  end
  return GainBias.subReleased(self, i, shifted)
end

function C:spotReleased(spot, shifted)
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
    self:setParamMode(false)
  end
  return GainBias.spotReleased(self, spot, shifted)
end

function C:zeroPressed()
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:zero()
    return true
  end
  return GainBias.zeroPressed(self)
end

function C:cancelReleased(shifted)
  if self.paramMode and self.paramFocusedReadout then
    self.paramFocusedReadout:restore()
    return true
  end
  return GainBias.cancelReleased(self, shifted)
end
```

For Pattern C controls (TransformGateControl, RatchetControl):
- Migrate from EncoderControl to GainBias if there is a natural "level"
  param to host the main fader. RatchetControl: probably no -- the main
  graphic is a ComparatorView. TransformGateControl: same. So these
  controls stay EncoderControl-based but adopt the **shift mechanics**
  above (shiftHeld / shiftUsed / snapshot) and use **subGraphic swap**
  instead of show/hide. They keep their immediate-toggle-when-no-readout
  behavior or drop it -- decide as part of Decision 1 implementation.

For Pattern B (BandListControl, ChaselightControl, DelayInfoControl,
HelicaseSyncControl, LaretClockControl): leave as-is. They have no second
submenu and no shift toggle is needed. Document them as the canonical
"single static submenu" pattern.

## Recommended execution order

1. Land decisions 1, 2, 3, 4 together as a single refactor PR. Affects
   Pattern A controls (only #4 touches the inverted-flag three) and
   Pattern C controls (TransformGateControl, RatchetControl).
2. Decision 5 as a follow-up touching every Pattern A subReleased.
3. Decision 6 deferred. Move Pecto package only after the spec has settled
   on hardware. Defer (c) until use cases for additional surfacing emerge.

## Open questions

- Does the framework guarantee `addSubGraphic` is called after every
  `removeSubGraphic`? The current `onCursorLeave` reset path in some
  controls calls remove without re-add (the next onCursorEnter or the next
  setParamMode does it). Verify before relying on this.
- For Pattern C → A migration: what does the main graphic look like for
  TransformGateControl and RatchetControl in normal mode? Today their main
  graphic is a ComparatorView; "normal submenu" is the gate/clock view.
  Without a host bias param these controls cannot adopt GainBias as a
  parent. The spec above accepts this and keeps them on EncoderControl
  with Pattern A's shift mechanics.
- Should `paramFocusedReadout` be unified with `focusedReadout`? Today
  Pattern A controls maintain two parallel fields. Merging them risks
  reintroducing the nil-deref class of bug because GainBias hooks deref
  `self.focusedReadout` unconditionally. Safer to keep them separate.
