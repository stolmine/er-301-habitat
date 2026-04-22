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

## Open questions (resolved)

- **Framework subGraphic lifecycle:** verified. `ViewControl.onCursorEnter`
  at `er-301-stolmine/xroot/Unit/ViewControl/init.lua:130-135` auto-calls
  `addSubGraphic(self.subGraphic)`; `onCursorLeave` at `:141-147` auto-calls
  `removeSubGraphic(self.subGraphic)`. This is precisely what makes the
  persistence fix simple: stop fighting the base class, leave
  `self.subGraphic` pointing at the correct mode's graphic across
  leave/return, and let the base class handle attach/detach.
- **Pattern C → A migration:** Pattern C controls (TransformGate, Ratchet)
  keep their EncoderControl parent. Their normal side is a ComparatorView
  (gate visualization), not a fader. Canonical Pattern A mechanics
  (shiftHeld/shiftUsed/snapshot + subGraphic swap) adopted.
- **paramFocusedReadout unification:** not pursued. Keep
  `paramFocusedReadout` separate from `focusedReadout` per the heap-
  corruption class-of-bug captured in `feedback_gainbias_dual_mode_focus.md`.

## Decisions locked (2026-04-21)

| # | Decision | Chosen |
|---|---|---|
| 1 | Encoder suppression vs snapshot-only | **B -- shiftUsed flag suppresses toggle on any encoder touch during shift-hold** (Pattern C adopts it) |
| 2 | Pattern C shift+spot secondary toggle | **Drop** -- tap-shift-alone is the only toggle path; shift+spot reserved for stock semantics |
| 3 | show/hide vs subGraphic swap | **Swap** -- Pattern C unified with Pattern A mechanics |
| 4 | Inverted mode-flag polarity | **Normalize to `paramMode`** -- rename FocusShapeControl / ScanSkewControl / MixInputControl |
| 5 | shift+sub in paramMode | **Option B -- opens keyboard for that readout** (uniform with stock GainBias) |
| 6 | Pecto migration | **Deferred** -- leave Pecto in biome with current pattern mix; revisit after the spec has settled on hardware |
| 7 | Sub-display default + persistence | **Default to GainBias sub-display on first entry. Preserve `paramMode` across cursor leave/return within a session. Do NOT serialize `paramMode` across quicksave. Reset `paramFocusedReadout = nil` on leave (vanilla convention: user must deliberately focus to edit). Pattern C mirrors: default to normal comparator view, persist mode across leaves, reset focused readout.** |

## Canonical Pattern A (amended for decisions 1-5 and 7)

```lua
self.paramMode = false               -- Decision 7: default to stock GainBias sub-display
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
  -- Decision 7: do NOT reset paramMode here. Mode persists from last leave.
  -- subGraphic is whatever we left it as; paramFocusedReadout is nil
  -- (cleared on leave), so GainBias.onCursorEnter has already refocused
  -- self.bias if we're in normal mode.
end

function C:onCursorLeave(spot)
  -- Decision 7: preserve paramMode + subGraphic ref. Clear only the
  -- per-session focus so re-entry lands in neutral "no readout focused".
  if self.paramMode then
    self.paramFocusedReadout = nil
    self:setSubCursorController(nil)
  end
  self:releaseFocus("shiftPressed", "shiftReleased")
  GainBias.onCursorLeave(self, spot)
end

function C:shiftPressed()
  self.shiftHeld = true
  self.shiftUsed = false                            -- Decision 1 (B)
  if self.paramFocusedReadout then
    self.shiftSnapshot = self.paramFocusedReadout:getValueInUnits()
  else
    self.shiftSnapshot = nil
  end
  return true
end

function C:shiftReleased()
  if self.shiftHeld and not self.shiftUsed then     -- Decision 1 (B)
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
  if shifted and self.shiftHeld then self.shiftUsed = true end  -- Decision 1 (B)
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
      -- Decision 5 (B): shift+sub opens keyboard for that readout
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
  -- Decision 2: no secondary toggle path. Just delegate when shifted
  -- is set so stock semantics apply. paramMode exit still happens on
  -- deliberate spot press in paramMode (cursor moves away).
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

**Pattern C (TransformGateControl, RatchetControl):** same shift mechanics
(shiftHeld / shiftUsed / snapshot, per Decision 1 B) and same
subGraphic-swap mechanics (per Decision 3). Default side is the normal
gate/comparator view (Decision 7 mirror). `spotReleased` no longer has a
secondary toggle path (Decision 2).

**Inverted-flag trio (FocusShape, ScanSkew, MixInput):** rename
`focusMode` / `scanMode` / `mixMode` to `paramMode`, flip the initializer
and every conditional (Decision 4). Behavior unchanged.

## Refactor execution plan

1. **PR 1 -- canonical Pattern A landing + inverted-flag normalization**
   (decisions 1, 3, 4, 7). Touches: 14 non-inverted Pattern A controls
   (shift mechanics sweep; persistence added); 3 inverted controls
   (renamed + Pattern A shift mechanics); 2 Pattern C controls
   (TransformGateControl, RatchetControl -- shift mechanics + subGraphic
   swap + drop-secondary-toggle per Decision 2). Tag Decision 2's
   shift+spot removal into this PR since it's a one-liner per Pattern C
   control.
2. **PR 2 -- Decision 5 rollout.** Touches every Pattern A subReleased to
   add the shift+sub -> keyboard-open branch. Single method per control.
3. **Decision 6 deferred.** No Pecto migration in this pass.

## Verification checklist per control

After each refactor, manually verify:
- [ ] Fresh insert: sub-display shows the correct side per the
      grandfather list (fader for 14 Pattern A non-inverted + FocusShape +
      ScanSkew + MixInput + TransformGate; custom for 4 Larets/Helicase
      overviews + CompBand + Ratchet).
- [ ] Tap shift with nothing focused -> toggles on release.
- [ ] Tap shift with readout focused and no encoder activity -> toggles on
      release.
- [ ] Hold shift, nudge encoder, release -> does NOT toggle (Decision 1 B).
- [ ] Hold shift, encoder +3 then -3, release -> does NOT toggle (this is
      the Pattern C footgun; verify Pattern C controls no longer exhibit
      it).
- [ ] Hold shift, tap spot (Pattern C only) -> no toggle (Decision 2).
- [ ] In paramMode, shift+sub opens keyboard (Decision 5 B; verify after
      PR 2).
- [ ] In paramMode, navigate cursor away and back -> mode preserved,
      no readout focused (Decision 7).
- [ ] Quicksave + reload: paramMode NOT persisted (Decision 7); unit
      restores in init-default mode.
- [ ] Cancel/zero/delete: preserve existing semantics; no regressions.

## Grandfathered defaults (user-confirmed, 2026-04-21)

The "default to stock GainBias on first entry" principle applies only to
controls that already do so. Current init values are preserved for
these controls (they already default to their custom sub-display and
there is a good UX reason -- each one's custom view is the unit's
headline UI or the primary parameter surface):

| Control | Init | Rationale |
|---|---|---|
| LaretOverviewControl | `paramMode = true` | step overview is the Larets viz |
| HelicaseOverviewControl | `paramMode = true` | phase-space + transfer curve is the Helicase viz |
| HelicaseModControl | `paramMode = true` | modulator params are the primary edit surface |
| HelicaseShapingControl | `paramMode = true` | disc-shape params are the primary edit surface |
| CompBandControl | `paramMode = true` (renamed from `compMode`) | comp threshold/ratio/speed is the unit's value; users come to edit these |
| RatchetControl | `paramMode = true` (renamed from `ratchetMode`) | ratchet params are the unit's identity; the gate is a secondary view |

The rule "Decision 7 default to GainBias" therefore applies to the 14
Pattern A non-inverted controls plus the three inverted-flag renames
(FocusShape, ScanSkew, MixInput -- all already effectively default to
stock) plus TransformGateControl (already defaults to gate-side stock).
Net: no init value flips in this refactor. Decision 7's contribution is
pure persistence.

## Execution plan (detailed)

### Affected files (21 total)

**Pattern A non-inverted (14 files)** -- persistence fix only (PR 1) plus
shift+sub keyboard branch (PR 2). No rename.

- `mods/spreadsheet/assets/DriveControl.lua`
- `mods/spreadsheet/assets/FeedbackControl.lua`
- `mods/spreadsheet/assets/MixControl.lua`
- `mods/spreadsheet/assets/RauschenCutoffControl.lua`
- `mods/spreadsheet/assets/TimeControl.lua`
- `mods/spreadsheet/assets/ParfaitMixControl.lua`
- `mods/spreadsheet/assets/CompMixControl.lua`
- `mods/spreadsheet/assets/LaretsMixControl.lua`
- `mods/biome/assets/DensityControl.lua`

Plus the four inverted-init controls already using `paramMode` (default
`true`, grandfathered):

- `mods/spreadsheet/assets/LaretOverviewControl.lua`
- `mods/spreadsheet/assets/HelicaseModControl.lua`
- `mods/spreadsheet/assets/HelicaseOverviewControl.lua`
- `mods/spreadsheet/assets/HelicaseShapingControl.lua`

Plus the 3-mode cycle variant:

- `mods/spreadsheet/assets/BandControl.lua` -- keep `paramMode = 0`
  cycle; add persistence; adopt `shiftUsed`.

**Pattern A flag rename (4 files)** -- field rename + persistence + Decision 5.

- `mods/spreadsheet/assets/FocusShapeControl.lua` -- `focusMode` -> `paramMode` (polarity flip to `paramMode=true=custom`; init flips `true -> false`).
- `mods/spreadsheet/assets/ScanSkewControl.lua` -- `scanMode` -> `paramMode` (same).
- `mods/spreadsheet/assets/MixInputControl.lua` -- `mixMode` -> `paramMode` (same).
- `mods/spreadsheet/assets/CompBandControl.lua` -- `compMode` -> `paramMode` (polarity preserved: `paramMode=true=comp-params-custom`; init stays `true` per grandfather rule).

**Pattern C migration (2 files)** -- rename + show/hide -> swap + shift-mechanics overhaul + drop shift+spot + persistence.

- `mods/spreadsheet/assets/TransformGateControl.lua` -- `mathMode` -> `paramMode` (init `false`, gate view stock).
- `mods/spreadsheet/assets/RatchetControl.lua` -- `ratchetMode` -> `paramMode` (init `true`, ratchet params default per grandfather).

**Pattern B (5 files, no change, documented as canonical static-submenu pattern)**

- `mods/spreadsheet/assets/BandListControl.lua`
- `mods/spreadsheet/assets/ChaselightControl.lua`
- `mods/spreadsheet/assets/DelayInfoControl.lua`
- `mods/spreadsheet/assets/HelicaseSyncControl.lua`
- `mods/spreadsheet/assets/LaretClockControl.lua`

### Shared helper for Decision 5 (numeric keyboard open)

Rather than duplicate the Keyboard.Decimal instantiation across 18
Pattern A controls, extract a helper. Based on `GainBias:doGainSet` at
`er-301-stolmine/xroot/Unit/ViewControl/GainBias.lua:705-721`:

```lua
local Decimal = require "Keyboard.Decimal"

local function openKeyboardFor(readout, desc)
  local kb = Decimal {
    message = string.format("Set '%s'.", desc or readout.name or "value"),
    commitMessage = string.format("%s updated.", desc or readout.name or "value"),
    initialValue = readout:getValueInUnits()
  }
  local task = function(value)
    if value then
      readout:save()
      readout:setValueInUnits(value)
    end
  end
  kb:subscribe("done", task)
  kb:subscribe("commit", task)
  kb:show()
end
```

Lives at `mods/spreadsheet/assets/shared/ShiftHelpers.lua` (new file),
required by each Pattern A control. Each control provides a small
`_paramReadoutForButton(i)` dispatcher that returns the Readout for
sub1/sub2/sub3 in paramMode.

### PR sequencing

**PR 1 -- persistence + renames + Pattern C migration** (decisions 1, 2, 3, 4, 7).
All 21 affected controls touched. Do not split further -- mid-migration
state would mean Pattern C and Pattern A behaving differently, which is
the status quo we are eliminating.

Within the PR, work in this order (smallest blast radius first):

1. Land the shared `openKeyboardFor` helper (`mods/spreadsheet/assets/shared/ShiftHelpers.lua`). No behavior change; preps the surface for PR 2.
2. Apply the persistence fix to the 14 Pattern A non-inverted controls. Only `onCursorLeave` changes.
3. Apply persistence to the 4 inverted-init (Larets/Helicase) controls.
4. Rename `CompBandControl.compMode` -> `paramMode` (polarity preserved, init `true`).
5. Rename + polarity-flip + persistence on the three inverted-flag controls (FocusShape, ScanSkew, MixInput).
6. Refactor BandControl: keep 3-mode cycle, add persistence, adopt `shiftUsed`.
7. Migrate TransformGateControl: build dual subGraphics, replace show/hide with swap, adopt Pattern A shift mechanics, drop shift+spot, apply persistence.
8. Migrate RatchetControl: same as TransformGate; preserve `paramMode=true` init.

Test at every step. Smoke test per control per the checklist above.

**PR 2 -- Decision 5 rollout.** Add `shift+sub -> keyboard` branch to
`subReleased` across the 18 Pattern A controls (not Pattern C --
TransformGate/Ratchet have discrete-valued sub-buttons like xform-func
and ratchet-len toggles where numeric entry is meaningless). One-method
edit per file using the shared helper.

### Critical gotchas

1. **Never assign `self.focusedReadout` directly.** Use `self:setFocusedReadout(self.bias)`. Direct assignment is the class-of-bug in `feedback_gainbias_dual_mode_focus.md` that caused heap corruption on delete.
2. **`paramFocusedReadout` can be nulled safely** (not a GainBias base-class field).
3. **`setParamMode` must guard `addSubGraphic` / `removeSubGraphic` on visibility.** Only call attach/detach when `self:getWindow()` is non-nil; otherwise just update state and let the next `onCursorEnter` re-attach.
4. **Shift release is dispatched to the last `grabFocus("shiftReleased")` caller.** If cursor moves mid-shift-hold, the release still goes to the original grabber. `releaseFocus` in `onCursorLeave` correctly clears that grab; do not skip it.
5. **BandControl's 3-mode cycle** needs a tiny wrapper: `setParamMode((paramMode + 1) % 3)` rather than boolean toggle. Verify that persistence-across-leave preserves the intermediate mode (user may end up on mode 2 after a cycle-away).
6. **Serialization is NOT affected.** `paramMode` is session-only per Decision 7. Existing ParameterAdapter Bias round-trips untouched.

### Risks

- **Heap corruption regression** if any `self.focusedReadout = ...` direct assignment sneaks in. Mitigation: grep the diff for `self.focusedReadout =` before merge; every hit should use the method.
- **Pattern C subGraphic swap** is new territory. Building two separate subGraphic instances for TransformGate and Ratchet at init is non-trivial. Budget the most time here. Toggle mode repeatedly during focus and verify no ghost children.
- **BandControl cycle + persistence** may surprise users (come back in mode 2 after leaving in mode 2). If that's not desired, special-case BandControl to reset to mode 0 on leave.
- **Pecto uses MixControl / TransformGateControl / DensityControl directly.** PR 1 touches all three. Pecto is NOT on the migration list (Decision 6 deferred), but must keep working. Verify Pecto on emulator after PR 1.

### Verification scope

- Per-control smoke test (this doc's checklist) after every file edit.
- Package-level regression on emulator after PR 1 and after PR 2, using `docs/test-procedures.md`.
- Hardware sign-off (am335x build + install) on one representative unit per affected package (Helicase for spreadsheet, Pecto for biome) before closing the task.

### Reference files (SDK)

- `er-301-stolmine/xroot/Unit/ViewControl/init.lua:130-147` -- ViewControl auto-attach/detach of subGraphic.
- `er-301-stolmine/xroot/Unit/ViewControl/GainBias.lua:635-646` -- `setFocusedReadout` (only safe way to assign).
- `er-301-stolmine/xroot/Unit/ViewControl/GainBias.lua:705-743` -- `doGainSet` / `doBiasSet` template for Decision 5.
- `er-301-stolmine/xroot/Unit/ViewControl/GainBias.lua:745-784` -- `subReleased` shift+sub reference.
- `er-301-stolmine/xroot/Keyboard/Decimal.lua` -- keyboard constructor + signals.
- `er-301-stolmine/xroot/Base/Widget.lua:56-66` -- `setSubCursorController`.
- `er-301-stolmine/xroot/Base/Widget.lua:87-140` -- `addSubGraphic` / `removeSubGraphic` / `grabFocus` / `releaseFocus`.
- `er-301-stolmine/xroot/Encoder.lua` -- `Encoder.Fine` constant.

## Per-unit test matrix

Unit-by-unit checklist for running the emulator smoke test. Each unit
below contains one or more affected controls; the key column lists the
specific things to probe so units that expose more surface area get more
attention. The shorthand in the "test focus" column maps to the
`verification checklist per control` section above:

- **F**: fresh-insert default side correct
- **T**: tap-shift toggles on release
- **N**: nudge-encoder-during-shift does NOT toggle (Decision 1 B)
- **SP**: shift+spot does NOT toggle on Pattern C (Decision 2)
- **K**: shift+sub opens keyboard in paramMode (Decision 5)
- **P**: paramMode persists across cursor leave/return (Decision 7)
- **Q**: paramMode does NOT persist across quicksave/reload
- **D**: no heap corruption / nil-deref on unit delete
- **Tog**: discrete-toggle sub branches still toggle on plain tap, don't
  open keyboard on shift+tap

Pattern C controls are **C** rows. Everything else is Pattern A.

### spreadsheet package

| Unit | Controls | Test focus | Status |
|---|---|---|---|
| **TrackerSeq (Excel)** | TransformGateControl (xform ply) | F(gate), T, N, SP, P, Q, D; in math mode tap sub1/sub2/sub3 focuses func/factor/fire — no keyboard on shift (xform sub-buttons are discrete-valued, Decision 5 does not apply to Pattern C). Verify xform gate ply still fires on shift+sub or func-encoder. | **pass 2026-04-21** |
| **GateSeq (Ballot)** | RatchetControl (ratch ply), TransformGateControl (xform ply) | F(RatchetControl=paramMode/ratch, TransformGate=gate), T on both, N on both, SP on both, P on both, D. Ratchet sub2/sub3 toggle len/vel options — shift+sub2/sub3 should NOT open keyboard (discrete toggles). | **pass 2026-04-21** |
| **Etcher** | TransformGateControl (xform ply) | Same as TrackerSeq xform. Also verify the per-segment step list still edits unaffected by this refactor. | **pass 2026-04-21** |
| **Filterbank (Tomograph)** | MixControl (mix ply) | F(fader), T, N, K(input/output/tanh subs), P, Q, D. | **pass 2026-04-21** |
| **MultitapDelay (Petrichor)** | FeedbackControl, MixControl, TimeControl, TransformGateControl (xform ply) | Highest surface-area unit. F across all four; T on all four; K on Feedback(tone), Mix(input/output/tanh), Time(grid/reverse/skew); xform ply same as Excel. Persist paramMode independently per ply (leaving Feedback paramMode on, cursor to Mix, back to Feedback → still on). Verify xform randomization still fires. | **pass 2026-04-21** |
| **MultibandSaturator (Parfait)** | DriveControl, BandControl (×3), ParfaitMixControl | F across all five plies; T on all five; K on Drive(tone/freq), each Band(amt/bias/type OR wt/freq/morph depending on cycle), Mix(comp/output/tanh). **BandControl 3-mode cycle** -- confirm cycling stops on shift-release with no encoder activity, and the cycle position persists across cursor leave. Watch for ghost-child visuals if cycling fast. | **pass 2026-04-21** |
| **MultibandCompressor (Impasto)** | DriveControl, CompBandControl (×3, grandfathered paramMode=true), CompMixControl | F(Drive=fader, CompBand=paramMode/comp, CompMix=fader); T on all; K on Drive(tone/freq), each CompBand(threshold/ratio/speed), CompMix(output only — sub1 is auto toggle, shift+sub1 should no-op). Tog on CompMix sub1 and CompSidechain subs. Also: stereo re-sync (existing Impasto bug is out of scope but worth verifying no regression). | **pass 2026-04-21** |
| **Rauschen** | RauschenCutoffControl | F(fader), T, N, K(morph/Q), P, Q, D. Verify algo fader label still refreshes on deserialize. | **pass 2026-04-21** |
| **Helicase** | HelicaseModControl (grandfathered paramMode=true), HelicaseOverviewControl (grandfathered paramMode=true), HelicaseShapingControl (grandfathered paramMode=true) | F(all three show custom on insert), T on all three, K on ModControl(ratio/feedback/shape), OverviewControl(mix/carrier — sub2 is lin/expo toggle, shift+sub2 no-op), ShapingControl(index/disc/type). Pattern's headline viz: overview phase-space viz still renders across cursor moves. Persistence critical here — leaving mid-edit should not snap back to stock. | **pass 2026-04-21** (one bug: sub-display selection indicator lands on sub3 after ply-to-ply navigation -- GainBias-inherited bias-at-sub3 auto-focus leaking into paramMode custom sub-display. Affects all Pattern A controls, not Helicase-specific. Tracked in todo.md) |
| **Larets** | LaretOverviewControl (grandfathered paramMode=true), LaretsMixControl, TransformGateControl (xform ply) | F(LaretOverview=paramMode, LaretsMix=fader, TransformGate=gate); T on all; K on LaretOverview(skew/steps/loop), LaretsMix(output/comp — sub3 auto is Tog), xform ply same as Excel. Step list chaselight unaffected. | **pass 2026-04-21** |
| **Colmatage** | ColmatageBlockControl, ColmatageRepeatsControl, ColmatageTextureControl, MixControl | F(all fader); T on all four; K on BlockControl(phrase min/max, block max), Repeats(ritard/blend/accel), Texture(amp min/max, fade), Mix(input/output/tanh). BSP tile-field overview viz unaffected. | pending |
| **Blanda** | FocusShapeControl, MixInputControl (×3), ScanSkewControl | F(all fader/mix-mode default); T on all five; K on FocusShape(shape1/2/3), MixInput(level/weight/offset in paramMode — sub1/2/3 in mix mode are branch/solo/mute, Tog), ScanSkew(skew only, sub2). Bell landscape viz unaffected. | **pass 2026-04-21** |

### biome package

| Unit | Controls | Test focus | Status |
|---|---|---|---|
| **Pecto** | MixControl, TransformGateControl, DensityControl (plus stock GainBias / Pitch plies unaffected: size, feedback, inputLevel, outputLevel, tanhAmt, tune) | **Biggest biome risk.** Pecto imports three spreadsheet controls directly. F across all three; T; K on Mix(input/output/tanh), Density(pattern/slope/resonator); xform ply same as Excel. NEON 3-pass gather path unchanged — CPU should still sit at ~6% stereo. | in progress 2026-04-21 |

### Other packages (MI ports, legacy)

Not touched by this refactor. Listed here only so nothing is missed on a regression sweep:

- **Plaits / Clouds / Warps / Rings / Stratos / Grids / Commotio / Marbles / Kryos** — no modified controls imported. Rings uses its own `rings.MixControl` (not `spreadsheet.MixControl`).
- **scope** — not touched.
- **catchall** (Sfera / Lambda / Flakes / Som) — experimental. Not in audit scope; Som uses its own Pattern-A-style controls that are structurally similar but not refactored. If time permits, sanity-check Sfera/Flakes still load.

### Cross-cutting verification (run once after all units tested)

- [ ] Quicksave a patch with every unit above inserted and with paramMode left in various states. Reload. Every unit should restore in its init-default mode (paramMode NOT serialized per Decision 7).
- [ ] Rapid insert/delete of Pecto, Petrichor, Parfait, Impasto 10x each. No heap corruption, no `malloc(): invalid size`, no nil-deref.
- [ ] `grep "self.focusedReadout = " mods/spreadsheet/assets/*.lua mods/biome/assets/*.lua` — every hit should be a read, never a write (writes must go through `self:setFocusedReadout(...)`). The one intentional exception is `self.focusedReadout = nil` in `onCursorLeave` / `upReleased`, which is safe.
- [ ] No `shiftDeferred` occurrences remain in spreadsheet or biome (legacy Pattern C idiom; replaced by `shiftHeld` / `shiftUsed`).
- [ ] No `show()` / `hide()` on ratchet-params or xform-params children in TransformGate / Ratchet (replaced by subGraphic swap).
