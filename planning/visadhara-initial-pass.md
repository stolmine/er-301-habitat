# Visadhara — initial-pass implementation plan

Status: **active**, 2026-05-01. Detailed phase plan persisted to repo
per `feedback_persist_plans_to_repo`. Companion to
`planning/bia-clone-scoping.md` (architecture-from-manual reference).

Working name: **Visadhara** (Sanskrit, "poison-holder" / "venom-stream";
also an epithet of Shiva for holding the world's venom). Avoids
third-party branding per `feedback_no_third_party_branding`. Display
title in toc.lua = `"Visadhara"`. C++ class = `Visadhara`. Files =
`Visadhara.{cpp,h}` / `Visadhara.lua`. Internal source-material
references restricted to comments calling out the public technical
manual the clean-room is based on.

## Package home — DECIDED: `spreadsheet`

Tier-1 alongside Helicase / Ngoma / Pecto / JF. Original-design
voices live in spreadsheet under generic names. Visadhara fits the
same pattern.

```
mods/spreadsheet/
├── Visadhara.h                    // top-level od::Object class
├── Visadhara.cpp                  // process() + top-level glue
├── visadhara/                     // header-only DSP subset
│   ├── README.md                  // attribution to JF voice + Ngoma + Helicase
│   ├── voice.h                    // 6-lane voice (jf::four::Voice adapt)
│   ├── folder.h                   // threshold-reflection folder w/ dynamic stages
│   ├── morph.h                    // sin→tri→saw→sq wave morph LUT
│   ├── pmm.h                      // Metal mode 3-op phase-mod operator
│   └── noise.h                    // LCG noise + octave decimation
├── VisadharaGraphic.h             // header-only viz (per feedback_no_out_of_line_virtuals)
└── assets/
    ├── Visadhara.lua              // Lua wrapper
    ├── VisadharaModeControl.lua   // Mode (Skin/Liquid/Metal) ply (Pattern A shift toggle)
    └── ...                        // standard ply controls reused from spreadsheet
```

Spreadsheet PKGVERSION bump: post-JF (likely 2.7.x → 2.8.0 on first
Visadhara ship).

## Top-level UI plan

Single horizontal scroll of plies on the main view. Modeled on
Ngoma's 7-ply pattern (V/Oct, Char, Sweep, Decay, Level, trig,
expanded). Visadhara's plies:

| Ply | Control | Notes |
|---|---|---|
| 1 | **trig** | Comparator-driven gate input; fires AR envelope on all 6 voices |
| 2 | **V/Oct** | Pitch CV (1V/oct via the standard Helicase 10x ConstantGain pattern); octave switch on shift sub |
| 3 | **harmonic** | Per-voice decay + amplitude scaling. Bipolar GainBias |
| 4 | **spread** | Harmonic-series ↔ prime-series overtone spacing. Bipolar GainBias |
| 5 | **morph** | Sin → tri → saw → sq continuous waveshape. Bipolar GainBias (or unipolar 0..1) |
| 6 | **fold** | Threshold-reflection folder; top-quarter mixes pulse train. Unipolar GainBias |
| 7 | **attack** | Tri-mode: noise (CCW) / instant (center) / slow (CW). Bipolar GainBias |
| 8 | **decay** | Global AR decay rate. Unipolar GainBias |
| 9 | **level** | Output level. Unipolar GainBias |
| 10 | **expanded** | Catchall for anything that doesn't fit; aux controls |

Mode (Skin/Liquid/Metal) lives on the **shift sub of V/Oct** as a
3-position option (octave switch is also there as an alternate mode
slot, like Ngoma). Or on a config menu — decide during Phase 1.

Total 10 plies, comparable to Alembic / JF.

## Implementation phases

### Phase 1 — Skin-mode skeleton (6-voice additive)

**Goal:** unit inserts on a chain, single Trigger + V/Oct, 6 tonal
voices in additive synthesis with Spread + Harmonic + Morph
parameters working. Decay AR envelope per voice. Skin mode only.
Validates the NEON-voice scaffolding port and the spread/harmonic/
morph mappings.

- [ ] `Visadhara.h` + `Visadhara.cpp` with `od::Object` boilerplate.
  Inlets: `mTrigger`, `mVOct`. Outlet: `mOut` (single mono).
  Parameters: `mPitch`, `mHarmonic`, `mSpread`, `mMorph`, `mDecay`,
  `mLevel`. (Attack / Fold / Mode added in later phases.)
- [ ] `visadhara/voice.h` — adapt `jf::four::Voice` pattern. 6
  tonal lanes via two `four::Voice` instances (lanes [g0:0..3,
  g1:0..1]); g1 lanes 2,3 masked off via gate=0.
- [ ] AR envelope per voice. Reuse JF's Transient mode logic exactly:
  trigger rising edge starts cycle; phase advances at decay rate;
  retrigger ignored mid-cycle. Single trigger fans out to all 6
  voices on the same edge.
- [ ] **Spread mapping**: position 0..1 maps to per-voice frequency
  multipliers. Anchors:
  - 0 (CCW): harmonic series → `[1, 2, 3, 4, 5, 6]`
  - 1 (CW): prime series → `[1, 2, 3, 5, 7, 11]`
  - Linear interp between.
- [ ] **Harmonic mapping**: position 0..1 maps to per-voice
  amplitude + decay-rate scalars:
  - At 0: only voice 1 active (amp=1, others=0)
  - 0..0.25: voice 2 fades in linearly
  - 0.25..0.625: voices 3-6 decay rates extended progressively
    (`decay_scalar[N] = lerp(0, 1, (pos - 0.25) / 0.375)`)
  - 0.625..1.0: voices 3-6 amplitudes faded in progressively
  - Locked numerical table for now; tune by ear during Phase 5.
- [ ] **Morph mapping**: position 0..1 blends through 4 anchor shapes
  via 256-entry LUT × 4 anchors (sine, tri, saw, sq). Per-sample
  scalar gather (matches JF CURVE LUT pattern, register-only).
  Header: `visadhara/morph.h`.
- [ ] `Visadhara::process()` — block-rate frequency calc from V/Oct
  + pitch + spread; per-sample voice update + envelope + waveshape +
  sum. Apply Decay envelope, scale by Level, write to mOut.
- [ ] Lua wrapper `Visadhara.lua` with first-pass plies (V/Oct, trig,
  harmonic, spread, morph, decay, level). Standard GainBias + Pitch
  + Gate ply types from spreadsheet.
- [ ] `toc.lua` entry under Spreadsheet category.
- [ ] SWIG `%include "Visadhara.h"` in `spreadsheet.cpp.swig`.
- [ ] Force-clean SWIG, build linux + am335x. Run NEON hint check
  (`tools/check-neon-hints.sh`) on `Visadhara.o`. Run lint
  (`tools/check-graphic-virtual-defs.sh`) on whole tree.
- [ ] Hardware test: insert, verify trigger fires audibly with
  reasonable percussion-voice character. Sweep Spread + Harmonic to
  confirm musical range.

**Deliverable:** working Skin-mode percussion voice on hardware.
Nothing fancy yet (no folder, no noise, no Liquid/Metal), but the
6-voice additive engine is live.

### Phase 2 — Folder + Attack tri-mode + noise oscillator

**Goal:** the threshold-reflection folder with dynamic stages +
amplitude compensation, the tri-mode Attack (noise burst / instant /
slow), and the 7th (noise) oscillator added to the mix.

- [ ] **Folder** in `visadhara/folder.h` (header-only inline so the
  Visadhara class stays vtable-clean per
  `feedback_no_out_of_line_virtuals`):
  ```cpp
  inline float fold(float x, float threshold) {
    int stages = 0;
    while (fabsf(x) > threshold && stages < kMaxFoldStages) {
      x = (x > 0.0f) ? (2.0f * threshold - x)
                     : (-2.0f * threshold - x);
      stages++;
    }
    return x * compensationGain(stages);
  }
  ```
  - Max stages: 8 (safety). Manual says "as many as will continue
    to fold" — match that.
  - Compensation gain table: precomputed for stages 0..8.
    Manually-tuned by listening or analytic.
- [ ] **Top-quarter pulse train**: when fold > 0.75, also mix in
  `sign(folded) * threshold * pulseAmount` where pulseAmount =
  `(fold - 0.75) * 4.0`. Listen-test calibrate.
- [ ] **Attack** parameter (bipolar -1..+1):
  - `attack < 0`: noise injection at trigger. Burst into the noise
    oscillator's amplitude for ~10-20ms.
  - `attack ≈ 0`: instant attack (envelope rises in 1 sample —
    classic analog-pop character).
  - `attack > 0`: linear AR rise time. Slow attack mode.
- [ ] **Noise oscillator** in `visadhara/noise.h`:
  - Linear congruential generator (LCG): `state = state * 1103515245 + 12345; out = (state >> 16) & 0x7FFF;`
  - Octave decimation: hold-and-output every Nth sample where N =
    `2^octave_index_from_pitch`.
  - Mix into the same 6-voice bus before the folder.
- [ ] Wire Attack + Fold parameters in Lua, add plies.
- [ ] Hardware test: confirm folder produces identifiable
  percussive timbre changes; pulse train mix audible at top
  quarter; Attack tri-mode behaves correctly at all three regions.

**Deliverable:** Skin mode complete with all original BIA Skin-mode
controls.

### Phase 3 — Liquid mode (pitch envelope)

**Goal:** Liquid mode toggles a per-trigger pitch envelope on top of
Skin mode for the "extra kick" character.

- [ ] Add `od::Option mMode{"Mode", 1};` (1=Skin, 2=Liquid, 3=Metal).
  Phase 3 wires Skin and Liquid; Metal stub returns silence in this
  phase.
- [ ] **Pitch envelope** in voice state: per-trigger transient that
  modulates all 6 voices' phase increment. Pattern from Ngoma:
  `pitchEnv = pitchEnvCoeff^(samples_since_trigger)`. Coefficient
  tuned for ~30ms decay.
- [ ] When Mode == Liquid: multiply per-voice base inc by
  `(1 + pitchSweepAmount * pitchEnv)` where pitchSweepAmount is
  a fixed quantity (~0.5 octave at peak, decaying). No user
  parameter for the sweep amount in BIA spec — just a fixed
  character.
- [ ] Mode ply ply on V/Oct shift sub (3-position option) OR
  config menu. Decide during implementation.
- [ ] Hardware test: switch between Skin and Liquid; verify
  audible kick character difference.

**Deliverable:** Liquid mode working alongside Skin.

### Phase 4 — Metal mode (3-op PMM pair)

**Goal:** "A pair of 3-operator phase-modulated oscillators for
producing metallic, noisy, and alien sounds." Reuses Helicase's
2-op FM scaffolding extended to 3-op closed-loop.

- [ ] **3-op PMM operator** in `visadhara/pmm.h`:
  ```
  op3.phase += op3.inc;
  op3_out = sin(op3.phase + op2.lastOut * mod23);
  op2.phase += op2.inc;
  op2_out = sin(op2.phase + op1.lastOut * mod12);
  op1.phase += op1.inc;
  op1_out = sin(op1.phase + feedback * op1.lastOut);
  voice_out = op3_out;
  ```
  - Helicase's polynomial sine works here. No NEON needed for
    this section since it's only 3-op × 2 pairs = 6 sines (vs
    24 for the additive bus). Scalar is fine.
- [ ] Two pairs of 3-op operators in parallel. Pair 1 base ratio
  `[1.0, 1.5, 2.0]`; pair 2 base ratio `[1.7, 2.3, 3.5]`.
  Listen-test tune.
- [ ] Spread parameter still applies — modulates the inter-pair
  ratio toward more inharmonic relationships.
- [ ] When Mode == Metal: bypass the Skin/Liquid 6-voice bus,
  output is sum of the two PMM pairs.
- [ ] Folder + Attack still apply post-PMM (the folder + post-fold
  envelope re-application is mode-agnostic).
- [ ] Hardware test: switch to Metal; verify metallic / inharmonic
  / aliasing-rich character.

**Deliverable:** Metal mode complete; all three BIA modes working.

### Phase 5 — Polish + ship

**Goal:** the percussive thump from post-fold envelope re-apply,
custom viz, hardware CPU profile, ship.

- [ ] **Final envelope re-application** post-folder per the manual's
  designer note: `"The final touch was to re-apply the overall
  envelope to the signal after the folder which gave back a lot of
  the dynamics that are lost when folding."` Implement as a separate
  `mFinalEnv` AR envelope mirroring the per-voice AR shape, applied
  to the post-folder signal.
- [ ] **Custom viz** in `VisadharaGraphic.h` (header-only inline):
  - Option A: 6 vertical bars representing per-voice amplitude
    contribution (depending on Spread + Harmonic + envelope state).
    Pulses on trigger.
  - Option B: spectrum-style frequency-bin display showing the
    overtone structure changing with Spread.
  - Option C: simple radial sweep tied to envelope.
  - Pick during Phase 5 implementation. Whatever it is, fully
    inline per `feedback_no_out_of_line_virtuals`.
- [ ] am335x objdump pre-flight: `tools/check-neon-hints.sh
  Visadhara.o` should show ≤ Ngoma-level hint count.
  Audit any out-of-line virtuals via
  `tools/check-graphic-virtual-defs.sh`.
- [ ] Trig LUT sweep: any `sinf`/`cosf` in package paths swapped for
  LUT (`feedback_package_trig_lut`).
- [ ] Hardware CPU profile under worst-case (Metal mode + audio-rate
  modulation + max fold). Confirm <15% one-core target.
- [ ] Test procedures entry in `docs/test-procedures-clean.md`.
- [ ] Vanilla compatibility check.
- [ ] Spreadsheet PKGVERSION bump (e.g. 2.7.x → 2.8.0).
- [ ] Release notes entry.

**Deliverable:** Visadhara feature-complete, hardware-stable, in next
spreadsheet release.

## Open design questions (parked for in-flight decisions)

1. **Mode placement**: V/Oct shift sub OR config menu? V/Oct shift
   sub is more discoverable (sub-display label visible) but
   competes with octave switch. Config menu is cleaner but hides
   mode switching. Lean toward V/Oct shift sub with a 3-position
   layout: Skin / Liquid / Metal cycle.
2. **Fold compensation curve**: linear / log / table-driven. Plan:
   8-entry table, calibrated by listening test in Phase 2.
3. **Liquid pitch sweep depth**: BIA's spec is a fixed character.
   We could expose as an aux parameter in expanded view if it
   feels valuable. Default: ~+1 octave at peak, decaying ~30ms.
4. **Metal mode operator topology**: 3-op chain vs 3-op closed-loop
   feedback. Try both; pick the one that sounds more "Metal."
5. **Octave switch range**: BIA has `Bass / Alto / Treble` at 2-oct
   spacings. Habitat could expose as a 3-position option or merge
   into the V/Oct's natural range. Probably 3-pos option for parity.
6. **NEON layout for the 6 voices**: same as JF (MultiVoice<2> with
   2 lanes masked) — no novel NEON design.

## Risk catalog

- **Folder amplitude compensation curve**: hand-tune; spec is vague.
  Plan: implement linear default, calibrate by listening.
- **PMM aliasing**: spec calls it `"who cares about aliasing"`. We
  match. Document behavior in test procedures.
- **Mode-switch click**: Mode switching while the unit is producing
  audio could click. Mitigate with a brief crossfade between mode
  outputs (~5ms) on switch.
- **Pitch envelope discontinuity** (Liquid mode): coefficient-based
  decay; reset state cleanly on retrigger to avoid ringing.

## Non-goals (explicit deferrals)

- **Per-voice pitch CV** (BIA has only a single Pitch input).
- **Sample loading / wavetable user-supplied** (BIA uses fixed
  internal LUTs).
- **MIDI / i2c integration** (habitat doesn't have i2c).
- **Polyphonic operation** (BIA is monophonic).
- **Stereo output** (BIA is mono out; we ship mono out, vanilla
  auto-wires both chains via duplicate Out wiring if needed).

## Phase commit version targets

- Phase 1 → spreadsheet 2.7.x.1 dev (or whatever JF lands at)
- Phase 2 → 2.7.x.2
- Phase 3 → 2.7.x.3
- Phase 4 → 2.7.x.4
- Phase 5 → 2.8.0 release

## Cross-references

- `planning/bia-clone-scoping.md` — architecture-from-manual
  reference (read this first for context).
- `planning/jf-initial-pass.md` — JF's 4-lane NEON voice pattern;
  Visadhara reuses this scaffolding.
- `project_ngoma_codex.md` — drum voice precedent + pitch sweep
  pattern + NEON discipline lessons (Liquid mode reuses).
- `feedback_no_out_of_line_virtuals.md` — class shape rule for
  Visadhara C++ + any custom Graphic.
- `feedback_neon_intrinsics_drumvoice.md` — class-member NEON
  storage pattern.
- `feedback_neon_hint_surfaces.md` — auto-vec + spill trap surfaces.
- `feedback_package_trig_lut.md` — no `sinf`/`cosf` in package draw
  paths.
- `feedback_no_third_party_branding.md` — name discipline.
- `feedback_persist_plans_to_repo.md` — why this doc exists in repo
  rather than ephemeral chat.

## Now starting

Phase 1 first. Goal: Skin-mode skeleton with 6-voice additive +
Spread + Harmonic + Morph. Will commit at end of phase before
moving to Phase 2.
