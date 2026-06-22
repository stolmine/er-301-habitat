# Canals — 4-input + normalling topology

Planning doc for the next major Canals refresh after the DSP redesign
landed in dev. Builds on the spreadsheet-package version (the
biome copy retired at biome 2.2.0.1).

**Status:** design (2026-06-22). Currently shipping at spreadsheet
2.7.1.41 (dev). Target audio behavior matches the physical Three
Sisters' ALL + per-block input structure with auto-normalling.

---

## The vision

Mirror the Three Sisters hardware front-panel input structure:

- **Main In (ALL)**: signal fed to all three filter blocks when no
  per-block patching is present.
- **Three dedicated per-block inputs**: LOW, CENTRE, HIGH — each
  acts as a normalling jack. Patching here overrides the ALL feed
  for that specific block.
- **No per-block input level controls**. Pure routing destinations;
  audio passes through at unity. Anything the user wants in terms
  of level shaping happens in the upstream subchain.
- **Overview ply with sub-display containing three subchain
  routing destinations** — LOW / CENTRE / HIGH each accessible as
  a patch point from the overview, no separate plies for each.
- **Config option to disable ALL altogether**: even when nothing
  is patched per-block, the ALL feed can be muted entirely. Useful
  when the user wants strict per-block routing with no shared
  source.

### Normalling rules

```
ALL enabled (default), per-block all unpatched:
  LOW block    ← ALL signal
  CTR block    ← ALL signal
  HIGH block   ← ALL signal

ALL enabled, LOW patched only:
  LOW block    ← LOW IN  (override)
  CTR block    ← ALL signal
  HIGH block   ← ALL signal

ALL enabled, all three patched:
  LOW block    ← LOW IN
  CTR block    ← CTR IN
  HIGH block   ← HIGH IN

ALL disabled, per-block all unpatched:
  LOW block    ← silence (0)
  CTR block    ← silence (0)
  HIGH block   ← silence (0)

ALL disabled, LOW patched only:
  LOW block    ← LOW IN
  CTR block    ← silence
  HIGH block   ← silence
```

Patched ALWAYS overrides ALL for that block. ALL feeds only the
unpatched blocks. Disabling ALL is a global "ignore main in" gate.

---

## Architectural changes

### Inlets

| Inlet | Role | New / existing |
|---|---|---|
| `In1` / `In2` | ALL signal (main input) | existing — repurposed semantically |
| `Low In` | per-block LOW input | new |
| `Centre In` | per-block CTR input | new |
| `High In` | per-block HIGH input | new |

Mono unit (Three Sisters is mono). `In1` in mono chains is ALL.
In stereo chains, the existing pattern of dual-instance Canals at
parallel-place positions still applies (per the existing
`feedback_stereo_pattern_selection` — Three Sisters is mono
hardware; per-channel stereo via dual-instance parallel placement).
The new per-block inputs are per-instance — each Canals instance
has its own LOW / CTR / HIGH in.

### Outlets (no change)

Existing 5 sub-outs unchanged:
1. Out (fader-morphed L)
2. Out R (R duplicate for mono chains)
3. LOW (parallel L tap)
4. CENTRE (parallel L tap)
5. HIGH (parallel L tap)

### Parameters (additions)

- `mAllEnabled` — `od::Option` (1 = ON default, 2 = OFF)
- `mLowPatched` — `od::Option` (1 = OFF default = use ALL,
  2 = ON = use Low In). Set from Lua via patched-state polling.
- `mCentrePatched` — same pattern
- `mHighPatched` — same pattern

Lua polls branch-chain unit count and writes these Options at UI
refresh rate. C++ reads at block-rate top of process().

### Process loop routing

Per-sample per-block input selection (block-rate snapshot of the
Options):

```cpp
// Block-rate snapshots
bool allEn      = (mAllEnabled.value() == 1);
bool lowPatched = (mLowPatched.value() == 2);
bool ctrPatched = (mCentrePatched.value() == 2);
bool hiPatched  = (mHighPatched.value() == 2);

// Per sample
float allSig = mIn.buffer()[i];
float lowSig = lowPatched ? mLowIn.buffer()[i]
                          : (allEn ? allSig : 0.0f);
float ctrSig = ctrPatched ? mCentreIn.buffer()[i]
                          : (allEn ? allSig : 0.0f);
float hiSig  = hiPatched  ? mHighIn.buffer()[i]
                          : (allEn ? allSig : 0.0f);

// Feed into each block's SVF cascade
processLow(lowSig);
processCentre(ctrSig);
processHigh(hiSig);
```

Per-block input is unity-pass (no level scaling). Anything the user
wants in terms of level shaping happens upstream in the subchain.

### Detecting "is patched" from Lua

ER-301 branches each own a chain (`self.branches.lowIn.chain`) and
that chain has a `units` table. The unit count tells us whether
anything is assigned.

Polling approach (similar to existing units that update sub-display
state from branch state):

```lua
function Canals:onUpdate()
  -- Called on UI frame from a Timer.subscribe("onUpdate"). Cost is
  -- minimal — just three table-length checks + three Option writes
  -- if anything changed.
  local op = self.objects.op
  local lowPatched  = #self.branches.lowIn.chain.units > 0
  local ctrPatched  = #self.branches.centreIn.chain.units > 0
  local highPatched = #self.branches.highIn.chain.units > 0

  -- Map booleans -> 1/2 (Option convention per
  -- feedback_option_vs_parameter; 0 is CHOICE_UNKNOWN sentinel,
  -- never use it).
  op:setOptionValues({
    LowPatched    = lowPatched  and 2 or 1,
    CentrePatched = ctrPatched  and 2 or 1,
    HighPatched   = highPatched and 2 or 1,
  })
end
```

Actual `chain.units` API name to be verified at implementation time
— may be a method like `getUnits()` or a field. Doesn't affect the
plan.

---

## UI design

### Overview ply (the headline)

Top-level ply hosting:
- A custom view control with the ply's primary readout area
  reserved for visualization (TBD what — could be:
  - simple input-routing status display: 3 small icons showing
    where each block is sourcing from (ALL or per-block patched)
  - or just a static text label "Canals" with mnemonic
  - leave the door open for a later viz pass)
- Sub-display: **three sub-buttons** labeled "low", "ctr", "high".
  Each sub-button taps a corresponding subchain input branch.

### Sub-display interaction model

- Pressing a sub-button (1/2/3) on the overview ply's sub-display
  enters the corresponding subchain — same UX as pressing S-button
  on a standalone-ply branch.
- M-press on a focused sub-button → assign a unit into that
  subchain (standard ER-301 assign UX).
- No level/gain knob for any of these. Pure patch destination.

This requires a custom `ViewControl` Lua class — similar pattern to
the `MirrorOverviewControl` already in spreadsheet, but the sub-
display buttons act as branch entry points rather than readout
focus targets. Modeled on whatever existing habitat ViewControl
exposes branch-as-sub-button (audit existing patterns first to
find the closest match — likely involves overriding `subReleased`
to call `branch:enter()` or similar branch-focus method).

### Other plies (unchanged from current Canals)

- Fundamental
- Span
- Quality
- Output (fader-morph)
- Mode (XOVER / FORMANT)

Existing 5 functional plies remain after the overview ply. Total:
6 plies in main view.

### Menu config

- New menu Option: **"ALL Input"** — choices: "Enabled" (default),
  "Disabled". Lives on the unit's menu (header press), not as a
  ply. Rare-touch toggle; menu placement keeps it discoverable
  but out of normal-use surface.

### Input ply visibility

The per-block input branches (`lowIn`, `centreIn`, `highIn`) don't
appear as their own top-level plies. They're only accessible
through the overview ply's sub-display sub-buttons. This is the
"consolidate the 3 inputs into one overview" intent.

Implementation: include the branches in `onLoadGraph` but omit
them from the `expanded` view list in `onLoadViews`. The sub-
buttons on the overview ply's sub-display handle access.

---

## Implementation phases

### Phase 1 — C++: new inlets + routing options

1. Add `od::Inlet mLowIn`, `mCentreIn`, `mHighIn` to `Canals.h`
2. Add `od::Option mAllEnabled`, `mLowPatched`, `mCentrePatched`,
   `mHighPatched`, all with `enableSerialization()` per
   `feedback_serialize_deserialize_pattern`
3. In constructor: `addInput` and `addOption` for each
4. In `Canals::process()`: read Options at block-rate top, apply
   per-block input selection in the per-sample inner loop

### Phase 2 — Lua: branches + option wiring

1. In `onLoadGraph`: add three ConstantOffset passthrough objects
   for the per-block inputs:
   ```lua
   local lowIn = self:addObject("lowIn", app.ConstantOffset())
   lowIn:hardSet("Offset", 0.0)
   connect(lowIn, "Out", op, "Low In")
   self:addMonoBranch("lowIn", lowIn, "In", lowIn, "Out")
   -- same for centreIn, highIn
   ```
2. In `onLoadViews`: do NOT add view controls for the three input
   branches. They're hidden from the ply layout.
3. Add menu controls for `AllEnabled` Option

### Phase 3 — Custom overview ply view control

1. Create `CanalsOverviewControl.lua` modeled on the input-routing
   visualization concept. Stock sub-button widgets; sub-button
   release routes the cursor to the corresponding subchain.
2. Pattern lookup: find an existing habitat ViewControl that uses
   sub-buttons to access subchains rather than focus readouts
   (audit `mods/spreadsheet/assets/*Control.lua` for candidates).
3. If no existing pattern is close enough, derive from
   `EncoderControl` and add custom sub-handling.

### Phase 4 — Patched-state polling

1. Subscribe to `Signal.onDisplayFrame` (55 Hz per
   `xroot/Application.lua:146`) for branch state polling
2. Compare `#branch.chain.units` per branch to the last-set state;
   write Options only on change
3. Or hook into `Branch.assignUnit` / `Branch.removeUnit` if a
   direct callback API exists — quicker than polling, more
   responsive to changes
4. Ensure routing changes audibly happen WITHIN one block of the
   UI-side flip (so user gets immediate audio feedback)

### Phase 5 — Audition + serialization round-trip

1. Insert unit, patch into LOW input, hear LOW block sourced from
   patched signal only
2. Confirm CTR and HIGH still get ALL when LOW is patched
3. Unpatch LOW → returns to ALL feed for LOW block
4. Toggle ALL Enabled OFF → all unpatched blocks go silent
5. Quicksave round-trip: per-block patches restore correctly,
   `AllEnabled` Option restores correctly

### Phase 6 — Documentation + visualization deferred

The overview ply graphic area is reserved for a future visualization
pass — could show input routing state, signal flow, or filter
response. For v1 of this topology, simple text/icon indicators
suffice; the headline viz comes later (similar to Mirror's iteration
arc).

---

## Open questions

1. **Branch state polling vs. callback**: which is the framework
   pattern for detecting patched state? Audit existing habitat
   units before committing.
2. **Sub-button labels**: "low / ctr / high" or "L / C / H" (3-char
   fits sub-button width better)? Decide during UI work.
3. **What does the overview ply's MAIN graphic show pre-viz?**
   Could be: filter response curve (like Tomograph), routing
   diagram, or just text. Picking "routing diagram" might be the
   most useful default — small visual showing where each block is
   sourced from (filled per-block icon if patched, ALL icon if
   sourced from ALL, dim if ALL is OFF and not patched).
4. **CV-modulate ALL toggle?** Probably no — keeping it as a static
   menu Option avoids audio-rate routing churn. Confirm.
5. **First-instance default behavior**: insert unit + nothing
   patched + ALL enabled = ALL feeds all three blocks. Verifies
   the existing behavior continues to work for users who don't
   engage the new structure.

---

## Risks

1. **Patched-detection latency**: if polling at 55 Hz, there's
   up to ~18 ms between patch event and audible routing change.
   Should be imperceptible.
2. **Quicksave compatibility**: existing user patches won't have
   the new per-block input branches; deserialize must handle
   missing branch state gracefully. Should be automatic since
   the branches are added at unit init.
3. **Sub-button UX clarity**: users may expect sub-buttons to
   focus readouts (the existing spreadsheet convention). Sub-
   buttons as subchain entry points is a different mental model;
   needs to be discoverable. Mitigation: clear labels, maybe a
   visual cue (different border treatment) on the overview ply's
   sub-display vs. paramMode sub-displays.
4. **Routing change clicks**: switching between ALL and per-block
   at block-rate could click if the two signals are at very
   different DC levels. Mitigation: short crossfade on routing
   transition (10ms ramp) inside the unit. Probably not needed —
   patching/unpatching is a UI event the user knows about.
5. **Custom ViewControl complexity**: a sub-button-as-branch-entry
   control is non-standard. Implementation will surface framework
   quirks. Budget extra time for Phase 3.

---

## Out of scope

- Stereo per-instance (still dual-instance parallel-place pattern)
- Visualization for the overview ply (deferred to its own iteration)
- Per-block input level controls (explicitly not in scope per
  user vision)
- 4× OS or sharper decimator (already deferred to other plans)
- SPAN curve + volume + low-band retention captures (carry-forward
  from existing plans, not blocking this topology work)
- Final habitat name / promotion to released cycle (Canals stays
  in spreadsheet dev throughout this work)

---

## Related references

- `planning/canals-spreadsheet-redesign.md` — original plan for
  the spreadsheet-side Canals work
- `planning/canals-audio-rate-mod.md` — Phase 1+2 audio-rate mod
  details
- `planning/canals-internal-external-calibration.md` —
  calibration protocol
- `planning/canals-span-volume-capture-checklist.md` — open
  capture battery
- `project_canals_redesign_state` memory — live snapshot
- `feedback_serialize_deserialize_pattern` — Options +
  enableSerialization pattern
- `feedback_option_vs_parameter` — 1/2 value convention for
  Options, never 0
- `feedback_addoutput_required_for_multiout` — addInput needed for
  every Inlet too (same surface)
- `feedback_stereo_pattern_selection` — Pattern A dual-instance
  pattern for Three Sisters (mono hardware)
- `feedback_parammode_convention` — shift-toggle convention if
  the overview ply also gets a paramMode option later
