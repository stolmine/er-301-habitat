# Visadhara — initial-pass implementation plan

Status: **shipped** at spreadsheet 2.6.2.35 (2026-05-14). Detailed
phase plan persisted to repo per `feedback_persist_plans_to_repo`.
Companion to `planning/bia-clone-scoping.md` (architecture-from-manual
reference).

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
| 2 | **V/Oct** | Pitch CV (1V/oct via the standard Helicase 10x ConstantGain pattern). **BAT octave switch (±2 oct, 3-pos: Bass / Alto / Treble) on shift sub** — Ngoma pattern. Cycles −2 / 0 / +2 oct. |
| 3 | **mode** | **CV-able** (Skin / Liquid / Metal). Habitat enhancement over original's hard switch (the original has only a panel toggle, no CV jack); we expose CV per the user's request. Continuous 0..2 Parameter; default behavior is **smooth crossfade** between adjacent modes (mode=1.5 = 50% Liquid + 50% Metal mix). Config menu option **`mode crossfade: smooth / snap`** lets users force original-fidelity hard-snap. |
| 4 | **spread** | Harmonic-series ↔ prime-series overtone spacing. Bipolar GainBias + CV. **Critical timbral knob** — without it the voice can't traverse from clean drum tones to metallic/cymbal territory. |
| 5 | **harmonic** | Per-voice decay + amplitude scaling. Unipolar 0..1 GainBias + CV |
| 6 | **morph** | Sin → tri → saw → sq continuous waveshape. Unipolar 0..1 GainBias + CV |
| 7 | **fold** | Threshold-reflection folder; top-quarter mixes pulse train. Unipolar GainBias + CV |
| 8 | **attack** | Tri-mode: noise (CCW) / instant (center) / slow (CW). Bipolar -1..+1 GainBias + CV |
| 9 | **decay** | Global AR decay rate. Unipolar GainBias + CV |
| 10 | **level** | Output level. Unipolar GainBias + CV |

**Total: 10 plies.** Comparable to Alembic (8) / JF (14 with gates).
Single-row horizontal scroll, no menu pages.

**Reduction candidates** (parked for a Phase 5 trim pass once the
voice is musically dialed):
- Could move **level** to expanded view (most habitat units inline it
  but a few don't).
- Could fold **attack** into a sub-display under another ply if the
  tri-mode behavior turns out to be a "set once" choice rather than
  a frequently-swept parameter.
- Could put **mode** on a sub-display (V/Oct shift sub already has
  BAT — could share or split). But mode is CV-able so it really
  wants its own ply for cable access.

Reduction will be informed by hardware play-testing in Phase 5.

### Mode-CV implementation detail

```cpp
// Mode parameter: continuous 0..2 (Skin=0, Liquid=1, Metal=2)
const float modeRaw = mMode.value();
const int snapMode = (int)mModeSnap.value();   // 0 = smooth, 1 = hard

float skinAmt, liquidAmt, metalAmt;
if (snapMode) {
  // Hard-snap: round to nearest, single mode active
  int m = (int)(modeRaw + 0.5f);
  if (m < 0) m = 0; if (m > 2) m = 2;
  skinAmt   = (m == 0) ? 1.0f : 0.0f;
  liquidAmt = (m == 1) ? 1.0f : 0.0f;
  metalAmt  = (m == 2) ? 1.0f : 0.0f;
} else {
  // Smooth crossfade: tent function across adjacent modes
  float c = modeRaw < 0.0f ? 0.0f : modeRaw > 2.0f ? 2.0f : modeRaw;
  skinAmt   = c < 1.0f ? 1.0f - c : 0.0f;
  liquidAmt = c < 1.0f ? c : (c < 2.0f ? 2.0f - c : 0.0f);
  metalAmt  = c > 1.0f ? c - 1.0f : 0.0f;
}

// Final mix:
out = skinAmt * skinBus + liquidAmt * liquidBus + metalAmt * metalBus;
```

In Phase 1 only Skin is implemented; modeRaw is treated as 0 always.
Phase 3 adds the Liquid bus and the smooth/snap dispatch; Phase 4
adds Metal.

## Implementation phases

### Phase 1 — Skin-mode skeleton (6-voice additive)

**Goal:** unit inserts on a chain, single Trigger + V/Oct, 6 tonal
voices in additive synthesis with Spread + Harmonic + Morph
parameters working. Decay AR envelope per voice. Skin mode only.
Validates the NEON-voice scaffolding port and the spread/harmonic/
morph mappings.

- [ ] `Visadhara.h` + `Visadhara.cpp` with `od::Object` boilerplate.
  Inlets: `mTrigger`, `mVOct`. Outlet: `mOut` (single mono).
  Parameters: `mHarmonic`, `mSpread`, `mMorph`, `mDecay`, `mLevel`,
  `mMode` (placeholder, treated as 0 in Phase 1), `mModeSnap`
  (od::Option, smooth=1 default). Octave: `od::Option mOctave{"Octave", 2}`
  with values 1=Bass(-2), 2=Alto(0), 3=Treble(+2). (Attack / Fold
  added in Phase 2.)
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

### Phase 6 — Sonic identity + control design (post-MVP)

By end of Phase 5 the unit is a faithful BIA clone. Phase 6 takes it
beyond clone status into something with its own voice. Plus the
control-surface work that's been accumulating.

**Sonic identity** — make Visadhara sound like Visadhara, not just
"Skin/Liquid/Metal cloned". Investigation directions:
- [ ] **Compound fold processing**: the current threshold-reflection
  folder is functional but dry. Explore stacking it with: a soft
  saturation pre-stage, post-fold harmonic exciter, asymmetric fold
  (different positive vs negative thresholds), bit-decimation or
  sample-rate-reduction in the fold path, or a wave-shaper after
  the folder. Goal is to give the Fold control a more distinctive
  sonic signature beyond "just a wavefolder".
- [ ] **Filter somewhere in the chain**: a SVF or simple LP/BP/HP
  swept by some control. Open questions:
    - Pre-fold (limits the harmonic content going into the folder)
    - Post-fold (tames the fold's high-frequency wash)
    - Pre-output (shapes overall character)
    - Mode-specific (different filter behavior per mode)
  Likely needs an extra control or sub-page since none of the
  existing knobs naturally maps to a filter cutoff.
- [ ] **Other distinctive processing** ideas to consider: ring
  modulation, frequency shifting, comb filtering between voices,
  granular smearing on long decays, per-voice phase distortion.
  Catalog and listen-test before committing.

**Pitch envelope work** (currently fixed +1 oct / 50ms exponential):
- [ ] **Defaults review**: hardware listen test alongside reference
  recordings. Current values match BIA-ish character but might be
  worth tuning for Visadhara's distinct identity.
- [ ] **Routing**: currently a flat per-voice frequency multiplier
  via `(1 + liquidSweepAmt × pitchEnv)`. Alternative routings:
    - Asymmetric per-voice depth (lower voices get more sweep)
    - Spread-dependent depth (more inharmonic ratios → more sweep)
    - Phase-modulation routing (pitch env modulates a low-rate LFO
      that detunes voices)
    - Routes only into PMM operator ratios in Metal mode (not just
      to skin/liquid voices)
- [ ] **Curvature**: currently exp(-t/τ). Alternative shapes worth
  trying — linear ramp, S-curve, double-exponential (fast initial
  attack of pitch sweep, slow tail), bouncing/wobbling decay for
  more "alive" character.
- [ ] **Maybe expose as submenu params** (depth, decay time, shape)
  once we know what the defaults should be.

**Control design challenge** — too much wants to live on the surface:
- [ ] Currently 10 plies on the main view (trig, V/Oct, mode, spread,
  harmonic, morph, fold, attack, decay, level). Already a lot.
- [ ] Phase 6 will add filter cutoff, pitch-env depth, fold-pre-sat,
  etc. — easily another 4-6 controls if we expose them all.
- [ ] **Decisions to make**:
    - Which of the existing 10 stay top-level?
    - Which can move to shift sub-displays (paramMode pattern from
      `feedback_parammode_convention`)?
    - Which can move to config-menu options (set-once)?
    - Should we adopt a multi-page main view (à la JF, Pecto) — no
      precedent for this in original-design voices yet.
- [ ] **Constraint**: BIA hardware has 8 panel knobs + 8 jacks.
  Habitat's UI affordance is tighter (single horizontal scroll +
  shift sub + config menu). Some BIA-faithful 1:1 mapping isn't
  possible; we have to abstract.
- [ ] Worth referencing: Helicase's expanded view, JF's 14-ply +
  gate-page layout, Pecto's expanded sub-display.

**Deliverable:** Visadhara with distinct sonic identity beyond BIA
clone, refined pitch envelope, and a control surface that's dense
but coherent.

## BIA-parity polish pass (post-Phase 5, pre-Phase 6)

Started 2026-05-12 in response to audition: unit "hits in every mode"
but the additive bus is a touch sterile compared to BIA. Three small
DSP touches between Phase 5 (feature-complete vs manual) and Phase 6
(sonic identity beyond clone) to close the analog-feel gap. All edits
in `mods/spreadsheet/Visadhara.h` (header-only per
`feedback_no_out_of_line_virtuals`); commit 3 also touches
`assets/Visadhara.lua` and `visadhara/voice.h`. Separate commits per
touch so each can be A/B'd or reverted independently.

### Commit 1 — asymmetric per-voice detune

Six fixed multipliers (~±3 cents asymmetric spread) in
`visadhara/voice.h` as `static const float kVoiceDetune[6]`. Applied
at the block-rate freqMult assignment in `Visadhara.h`:

```cpp
s.freqMult[i] = visadhara::spread_mult(i, spreadPos)
              * visadhara::kVoiceDetune[i];
```

Asymmetric (not symmetric) so beat patterns stay irregular under
chord-style spread sweeps. Detune compounds with `pitchSweep` in
Liquid mode naturally. Zero per-sample cost (already-multiplied into
freqMult). No user control (per ask).

Version: spreadsheet 2.6.2.2 → 2.6.2.3.

### Commit 2 — dedicated post-fold final envelope

Replaces `s.env[0]` proxy with a dedicated `finalEnv` AR mirroring
the master attack/decay shape. Per BIA designer note:
*"re-apply the overall envelope to the signal after the folder which
gave back a lot of the dynamics that are lost when folding."*

Internal struct gains `float finalEnv = 0.0f`. Trigger handler sets
it to 1.0f (instant attack) or 0.0f (slow attack — rides
`s.slowAttack`). Per-sample update mirrors per-voice pattern with
decayScale=1. End-of-loop post-fold env multiply uses `s.finalEnv`
instead of `s.env[0]`. Decay coefficient and slow-attack ramp are
shared with voices (no new `expf` calls). Cost: ~3 muls + 1 add per
sample.

Version: spreadsheet 2.6.2.3 → 2.6.2.4.

### Commit 3 — per-hit micro-variation (menu-toggleable)

Sub-percent jitter on per-voice freq, decay coeff, and fold drive per
trigger. Breaks digital exactness without changing the unit's
character envelope.

New `od::Option mDrift{"Drift", 1}` (1=on / 2=off, per
`feedback_option_vs_parameter`), `enableSerialization()` in C++ ctor.
Internal struct gains `uint32_t rng`, `float jitterFreq[6]`,
`jitterDecay`, `jitterFold` (init 1.0).

Block-rate setup reads option, derives `driftAmt = (val == 1) ? 1.0f : 0.0f`.
At rising edge, LCG step from `noise.h` rolls 8 jitter scalars:

- per-voice freq jitter: ±0.3% × driftAmt
- decay coeff jitter: ±5% × driftAmt
- fold drive jitter: ±5% × driftAmt

`driftAmt = 0` collapses every jitter to 1.0 (no-op multiply). Same
codegen path on / off, branchless.

Per-sample usage: three multiplicative applications in the inner
loop (voice freq, voice decay coeff, fold drive). Cost: ~8 muls/sample.

Lua adds `driftHeader` + `drift` `OptionControl` to the config menu
("on" / "off"). Description strings stay bare per
`feedback_no_parenthetical_descriptions`.

SWIG dep gap risk per `feedback_swig_header_dep`: adding mDrift
changes Visadhara's sizeof. Force-clean wrapper before build:
`rm testing/{linux,am335x}/mods/spreadsheet/spreadsheet_swig.{cpp,o}`.

Version: spreadsheet 2.6.2.4 → 2.6.2.5.

### Risk summary

All three touches:
- Inside the existing per-sample loop (already
  `optimize("no-tree-vectorize")`).
- Use Internal struct fields (heap-allocated, per
  `feedback_neon_intrinsics_drumvoice`).
- No new branches in the loop (per
  `feedback_runtime_branched_dsp_dispatch`).
- No new stack-locals, no NEON intrinsics.

So the NEON `:64` hint surface and runtime-branched DSP traps from
the Ngoma bisect history do not apply.

### Follow-ups not in this pass

- Noise oscillator wire-up (still unused — `noise.h` exists; the
  micro-variation commit reuses its LCG helpers, not the sample-and-
  hold path).
- Folder drive curve shape (linear 1×..6× currently). Phase 6
  sonic-identity territory.

## 2× oversampling pass (post-BIA-parity)

Started 2026-05-12. Audition feedback: detune + post-fold env were
wins, but Visadhara still reads as "sum of its parts" not a refined
whole. User identified two specific symptoms: the folder "breaks up
before we can hit the same kind of folding depth that BIA gets," and
the pitch envelope feels "highly diluted."

Root-cause analysis (folder side): Visadhara runs the entire DSP at
the framework's 48 kHz output rate with **no oversampling**. The
signal chain generates massive HF content — raw saw/square in the
morph, threshold-reflection folder (each fold a fresh discontinuity),
top-quarter pulse-train injection, PMM in Metal. At 48 kHz Nyquist
the HF content aliases back into audible band as inharmonic mush.
What presents as "breaking up before deep fold depth" is aliasing
overwhelming musical fold-stage character. Analog BIA has no Nyquist
and no such ceiling.

Precedents in codebase: Helicase (RELEASE-2.3.0) added 2× oversampling
on its hi-fi inner loop with a 2-tap halfband decimator for click
suppression on discontinuity shapes. Ngoma uses the same 2-tap MA
decimator pattern per `project_ngoma_codex`. Visadhara is the natural
next adopter — its bus generates more HF content than either.

Scope: wrap the entire Visadhara per-sample inner loop in a 2×
k-iteration shell, decimate at output via 2-tap MA. Always-on (no
lo-fi/hi-fi toggle — CPU budget per audition is acceptable). All
time-integrating coefficients (decayCoeff, pitchEnvCoeff, slowAttackInc)
recomputed for 2× rate. Voice and PMM phase increments use `invSrOs =
invSr * 0.5f`. Trigger detection stays at output rate (gate edges are
output-sample-aligned).

What does NOT change:
- Block-rate setup (freqMult, harmonic params, morph weights, mode
  dispatch, jitter rolls) stays at output block rate.
- Internal struct layout — no new fields.
- Lua surface — no new controls.
- SWIG wrapper — no force-clean needed (no class shape change).
- NEON / register pressure — purely scalar change, no new intrinsics,
  no new stack-locals.

Risk: CPU roughly doubles. Pre-2× estimated ~10–15% per instance;
post-2× expected 20–30%. Helicase hi-fi precedent shows this is
acceptable on Cortex-A8. Profile after; if it overshoots 35% per
instance, fall back to polyBLEP-only mitigation on the discontinuity
shapes.

Version: spreadsheet 2.6.2.6 → 2.6.2.7.

### NEON voice bus + 8-voice extension (post-OS, this commit)

Followed the 2× oversampling pass. Audition: OS audibly fixes the
folder-breakup; CPU roughly doubles. User asked whether NEON can
recover the cost, and whether to fill the masked-off lanes while
NEON is in the air.

Two oversamples can NOT be packed into parallel NEON lanes — they're
state-dependent sample-to-sample (phase / env / pitchEnv / finalEnv
all integrate forward). NEON opportunity is the per-oversample voice
bus, currently scalar 6-iter and the dominant ~100 ops chunk of
inner-loop cost.

Phase 1 design intent was 4-lane × 2 NEON with 6 active + 2 masked.
Arrays already sized `float[8]` as heap class members
(`feedback_neon_intrinsics_drumvoice` compliant). What shipped was
scalar; this commit lands the NEON pass that was originally specified.

8-voice extension is genuinely free in the 4×2 NEON layout (same
quad ops either way, just with the previously-masked lanes now
contributing). User invited this in their request. Two more
harmonics (7th + 8th in harmonic mode; primes 13 + 17 in prime mode).

Scope:
- `visadhara/voice.h`: extend `kHarmonicSeries`/`kPrimeSeries`/
  `kVoiceDetune` to 8 entries; `harmonic_voice_params` voice-fraction
  divisor 3 → 5 (covers voices 2-7 across the same activation range).
- `visadhara/morph.h`: add NEON 4-lane `sample_w_4(float32x4_t phase,
  const Weights &w)`. Branchless via `vbslq_f32` masks for piecewise
  tri/sq. Inlined to avoid live-across-call register spills per
  `feedback_neon_hint_surfaces`. Scalar fallback for linux path.
- `jf/neon_shim.h`: extend with `vmlaq_f32`, `vabsq_f32`, `vrecpeq_f32`
  for linux build.
- `Visadhara.h`: per-half-sample voice loop replaces scalar 6-iter
  with 2× NEON 4-lane passes (8 lanes total). Drop `freqMult[6/7]=0`
  masking. `voiceGain` 0.5 → 0.375 to compensate for 8-voice peak.
- Internal struct: NO new fields (storage already correct).

Out of scope (deferred):
- PMM-pair NEON (Metal mode chains, sequential within-chain limits
  parallelism to 2 lanes; smaller win, separate commit if needed).
- Pitch envelope rework (depth asymmetry + 50 ms → 150 ms).

CPU target: drop from 2× baseline (2.6.2.7 cost) to ~1.4-1.5×
baseline. ~25-30 percentage points recovered of the OS penalty.

Verification: objdump pre-flight on am335x to confirm zero new `:64`
NEON hints in Visadhara symbols. Emu A/B vs 2.6.2.7. Hardware CPU
spot check.

Version: spreadsheet 2.6.2.7 → 2.6.2.8.

### PMM 2-pair NEON + 8-voice trigger reset fix

Followed the voice-bus NEON pass. Hardware audition confirmed
~30% CPU drop on the voice loop NEON work; user wants to push NEON
further wherever we can. Audit also surfaced a correctness bug from
2.6.2.8: trigger-handler loops still iterate `n < 6` while 8 voices
are active. Voices 6 and 7 sit at env=0 across triggers in
instant-attack mode (the default), so the audition was hearing a
6-voice bus, not 8.

Scope:

- Fix trigger handler 6→8 loops on phase[] and env[] resets.
- Reorganize PMM state in Internal struct from `Voice pmm1/pmm2`
  to NEON-friendly `float pmmPhase[3][4]` + `float pmmLastOut[3][4]`
  (op outer, pair-lane inner, lanes 2/3 padded).
- Block-rate pack PMM incs/mod-fb coefficients into Internal arrays
  so per-sample NEON loads don't need scalar-to-lane moves.
- Write `visadhara_pmm::tick2()` — NEON 2-lane-across-pairs op-step
  pattern, sequential within-chain (op2 reads op1's just-written
  lastOut, op3 reads op2's). Reuses `sample_w_4` morph helper.
  `always_inline` to keep register pressure consistent.
- New `wrap01_4` NEON helper handles negative phase wrap via
  truncating cast + negative-adjust (no `vrndmq` on Cortex-A8).

PMM ticks every half-sample regardless of Mode (smooth-crossfade
architecture gates output via metalAmt, doesn't skip the compute).
So PMM NEON savings apply to all patches, not just Metal mode.

Expected: PMM op count roughly halves. ~5-10 percentage points of
total inner-loop CPU recovered on top of the 30% in hand.

Out of scope:
- Folder NEON (iterative, no within-sample parallelism).
- Global envelope pack (saves ~3 muls per half-sample; trivial).
- Scalar morph helper force-inlining (linux-only path; not a target).
- Within-chain PMM parallelism (op2 reads op1's current output;
  sequential dependence).

Version: spreadsheet 2.6.2.8 → 2.6.2.9.

### Visadhara Corona viz — phased build (2026-05-13)

After Phase 1+2 of UI refinement (V/Oct octave sub, Mode ply with
spread/harm/morph paramMode subs), Phase 3 lands the signature
viz on Mode's main fader area. Concept: "Visadhara Corona" — a
2D circular polar oscilloscope with layered decoration.

Three layered elements + a trigger flash:

  1. Polar waveform (constant): 64-point polar polyline reading
     a decimated audio-output ring buffer. Line width via Harmonic.
     Trail/phosphor via Decay. Attack character emerges from
     wave shape; pitch/octave from angular density. Always
     animating.
  2. Concentric rings (Spread): 0-5 faint outline rings,
     decorating but not competing with the wave.
  3. Background plate (Mode + Fold): regenerates on each trigger.
     Brightness inverse-related to Fold (Fold = continuous, gives
     a contrast-crossing point with the wave). Mode shifts hue /
     texture across Skin / Liquid / Metal.
  4. Trigger flash: ~50 ms brightness pulse on rising edge.

Sub-phasing for incremental commits:
  - 3a (this commit): stub graphic + audio buffer plumbing. Polar
    waveform only — no rings, no background, no trail. Verifies
    data flow C++ → graphic + polar rendering math + LUT.
  - 3b: background plate with Fold-driven brightness, Mode-driven
    character.
  - 3c: concentric rings from Spread.
  - 3d: trail/decay accumulation + Harmonic line width.
  - 3e: trigger flash.

Each phase = one commit, individually auditable.

Implementation notes:
  - Graphic header-only (VisadharaCoronaGraphic.h) per
    feedback_no_out_of_line_virtuals.
  - 64-entry kCoronaCos / kCoronaSin LUT at file scope; pre-
    computed values, no runtime sinf/cosf in package code per
    feedback_package_trig_lut. (FilterResponseGraphic precedent.)
  - C++ Internal struct gains vizBuf[64], vizWriteIdx,
    vizSampleCounter. process() captures one sample per
    kVizSampleInterval (256 → ~187 Hz update rate; 64 samples =
    ~340 ms of recent audio).
  - Visadhara::getVizSample(int age) inline method inside
    #ifndef SWIGLUA gates Internal access; called from
    VisadharaCoronaGraphic::draw via mpVisadhara pointer.
  - VisadharaCoronaGraphic constructor + follow() pattern mirrors
    HelicaseOrbitalGraphic exactly.
  - SWIG dep gap: Visadhara class layout changes (added Internal
    fields). Force-clean wrapper before build.

Version: spreadsheet 2.6.2.18 → 2.6.2.19 for Phase 3a.

### UI refinement to 7 plies + Mode-viz (2026-05-13)

Sound design wraps up at 2.6.2.15. Next: clean up the ply surface
and add the signature graphic that every other spreadsheet voice
has.

Current state: 10 plies on the main view (trig, V/Oct, mode, spread,
harmonic, morph, fold, attack, decay, level) + 2 menu items (octave
BAT switch, mode crossfade snap). No custom viz; every ply uses
stock fader.

Target: **7 plies + viz** (Attack stays top-level — too timbrally
important to bury on a sub):

  1. trig
  2. V/Oct (+ octave on shift sub: Bass/Alto/Tenor)
  3. Mode (viz on main fader area; shift subs for spread/harm/morph)
  4. fold
  5. attack
  6. decay
  7. level

Octave moves out of menu (only ModeSnap remains there). 3 controls
(spread, harmonic, morph) become subs of Mode.

Implementation pattern: **`app.Readout:addThresholdLabel(threshold,
"text")`** — SDK-built feature on the Readout widget. Used in
Rauschen's `CutoffControl` (morph sub-readout flips between
"off"/"LP"/"L>B"/"BP"/"B>H"/"HP"/"H>N"/"ntch" as parameter crosses
thresholds). Same trick works on the V/Oct octave sub and on the
expanded-view fader. No new helper needed — pattern is built in.

For the V/Oct octave sub, octave converts from `od::Option`
(discrete 1/2/3) to `od::Parameter` (continuous, CV-able, snapped
via DialMap stepping at the readout). Bonus CV input capability;
required for ParameterAdapter / threshold-label compatibility.

#### Phasing

  Phase 1 (this commit): V/Oct + octave shift-sub.
    - C++: mOctave Option → Parameter
    - New VisadharaPitchControl.lua (modeled on Ngoma's
      DrumVoicePitchControl + Rauschen's addThresholdLabel)
    - Threshold labels: Bass / Alto / Tenor
    - Expanded view of V/Oct ply also shows octave (ThresholdFader)
    - Octave drops from config menu

  Phase 2 (later): Mode ply with viz + 3 shift subs (spread, harm,
    morph). Drops spread/harmonic/morph from main view. 10 → 7
    plies.

  Phase 3 (later): Custom viz graphic for Mode ply. Candidates
    (voice cluster bloom, mode triangle, phase circle) parked for
    audition selection.

  Version: 2.6.2.15 → 2.6.2.16 for Phase 1.

### Harmonic redesign: voice distribution + correlated detune (2026-05-13)

Audition feedback at 2.6.2.11: tighter than 2.6.2.10 but still not
all the way to "punchy." User's structural proposal: **all 8 voices
always play; Harmonic controls voice DISTRIBUTION (sub-cluster ↔
harmonic-series) AND correlated detune amount (full ↔ near-zero).**

Punch is sacrificed as Harmonic rises — natural consequence of
voices spreading away from the fundamental. At Harmonic=0, all
8 voices stack at 1× with full detune for a fat chorused sub
(maximum kick weight). At Harmonic=1, voices spread to harmonic
series with detune collapsed for clean integer-ratio relationships.

New control semantics:

  Harmonic = 0:
    - All 8 voices at 1× ratio (fundamental cluster)
    - Full per-voice detune (kVoiceDetune values)
    - All voices bend in lockstep (effective sweep weight = 1.0)
    - Single unified tau (25 ms) — coherent bend gesture
  Harmonic = 1:
    - Voices at full harmonic / prime series (kHarmonicSeries /
      kPrimeSeries blended via spreadPos)
    - Detune → 0 (clean integer ratios)
    - Asymmetric per-voice sweep weights (kSweepWeight: voice 0
      bend at 1.0, upper voices tapered to 0)
    - Per-voice tau (kSweepTauMs: 25/15/10/6/4/0/0/0)
  Intermediate: linear interpolation across all four axes.

Implementation:

  Visadhara.h block-rate freqMult loop replaces remap_harmonic +
  spread_mult + kVoiceDetune chain with linear-interp formulas:

    effectiveRatio  = 1 + harmonicPos × (spread_mult(i) - 1)
    effectiveDetune = 1 + (kVoiceDetune[i] - 1) × (1 - harmonicPos)
    freqMult[i]     = baseFreq × effectiveRatio × effectiveDetune × invSrOs

  Block-rate pitchSweepGainLanes / pitchEnvCoeffLanes similarly
  interpolate weight (1 → kSweepWeight[i]) and tau (25 ms →
  kSweepTauMs[i]) across harmonicPos.

  harmonic_voice_params is no longer called: ampScale[i] = 1,
  decayScale[i] = 1 for all voices unconditionally. Function
  retained in voice.h for documentation / legacy paths but unused.

  remap_harmonic call dropped from the freqMult loop — the new
  semantics use raw harmonicPos linearly. PMM Metal mode keeps
  its existing harmonicPosUser dependence (unchanged).

Cost: same NEON inner loop, only block-rate setup changes (a few
extra multiplies and one lerp per voice). Negligible.

Risk: voiceGain currently 0.375 — at Harmonic=0 all 8 voices
coherent at fundamental yields larger peak than the spread-out case
it was calibrated for. May need to reduce to 0.25 if the bus is
too hot driving the folder; audition will tell.

Version: spreadsheet 2.6.2.11 → 2.6.2.12.

### Pitch envelope rework (per-voice asymmetric sweep) — this commit

Followed the NEON wins. Structural diagnosis of why the Liquid-mode
pitch bend feels diluted rather than punchy:

1. **No figure/ground.** All 8 voices ride a single scalar
   `pitchSweep` factor — the entire harmonic series shifts as a
   rigid block. Ear hears "the whole spectrum bent" not "the
   fundamental bent." Punch requires ONE element carrying the
   gesture while the rest stay as stable timbral support.
2. **Upper-voice frequency wash.** Voice 7 sweeping 880→1760 Hz
   creates a brighter / more-audible motion than voice 0's
   110→220 Hz bend. The ear locks onto the brighter motion, which
   reads as a synth-pad whoosh rather than a kick thump.
3. **Exponential frontloads to 20 ms.** `expf(-1/(0.050*sr))` puts
   most of the bend energy in the first 20 ms — the perceptual
   gesture is over before the listener registers "there was a
   bend." Reads as a tonal artifact at the very start, not a
   gesture.

Compounding effect: any one in isolation might still produce punch.
Combined, the bend is short, spread across all voices, dominated
by the harmonics' wash. Result: diffuse.

The fix is structural, not parameter-tweaking. Per-voice
asymmetric sweep depth + per-voice time constants, implemented as:

- `visadhara/voice.h`: `kSweepWeight[8] = {1.0, 0.55, 0.25, 0.10,
  0.05, 0, 0, 0}` — voice 0 carries the gesture; upper voices
  silent contributors. `kSweepTauMs[8] = {200, 140, 80, 40, 25,
  0, 0, 0}` — slow settle on the fundamental; quick on the
  inflection partials.
- `Visadhara.h` Internal: replace scalar `pitchEnv` with
  `pitchEnvLanes[8]`. Add `pitchEnvCoeffLanes[8]` and
  `pitchSweepGainLanes[8]` (block-rate precomputed).
- Block-rate setup: derive per-voice decay coefficients from
  `kSweepTauMs` and per-voice gains from `liquidAmt × kSweepWeight
  × kPitchSweepPeak`. Coefficient=0 collapses to no decay (env
  stays at 1) but gain=0 for those voices anyway → no audible
  sweep contribution.
- Trigger handler: NEON vst1q to set pitchEnvLanes[0..7] = 1.0
  on rising edge.
- Per-half-sample NEON, per voice-loop pass: load lanes' pitchEnv,
  multiply by coeff (per-voice decay), compute
  `sweepLanes = 1 + gain × pitchEnv`, then advance phase as
  `p += freqMult × sweepLanes` (replaces the prior global scalar
  `pitchSweep`).

Cost: ~4 extra NEON ops per pass × 2 passes × 2× OS = 16 extra
NEON ops/output sample. Negligible.

Out of scope:
- Bend onset delay (3-5 ms attack-then-decay shape on pitchEnv).
  Park; revisit if depth+tau split doesn't fully nail "punchy."
- Shape alternatives (linear / S-curve / double-exp). Same gate.
- Cross-mode injection thinning (negative-Attack region) — touched
  on only when user pushes Attack into negative; orthogonal to
  the main complaint.

Risk: Internal struct grows by 96 bytes (24 floats × 4); force-clean
SWIG. New NEON loads in inner loop may pressure registers; lint check
required.

Version: spreadsheet 2.6.2.9 → 2.6.2.10.

## Open design questions (parked for in-flight decisions)

1. ~~**Mode placement**~~ — RESOLVED 2026-05-02. Mode is its own
   top-level ply, CV-able. BAT octave gets the V/Oct shift sub.
   Mode crossfade default smooth, hard-snap behind config option.
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

---

## Corona viz — geometric pivot (supersedes polar-scope plan above)

The 2.6.2.18 polar-oscilloscope concept (lines 621-673) was
abandoned: Helicase's slew/trail infra is tuned for steady-state
oscillators, not transient kicks — the wave never travelled off
the surface of the circle (2.6.2.19-2.6.2.22, all failed). The
audio-capture vizBuf was removed at 2.6.2.23.

Replacement: a **spirograph / arabesque carousel**. N vertical-
standing K-gons orbit a tilted horizontal carousel, each spinning
on its own vertical axis. Header-only (VisadharaCoronaGraphic.h),
LUT trig, no audio capture — reads param state directly.

### Mappings (live)

- **Spread → N**: petal count, integer 1..8.
- **Mode → Kf**: polygon sides as a FLOAT 3.0..8.0, rendered via a
  continuous radial function (coronaRadius crossfades floor/ceil
  integer polygons) so Mode sweeps smoothly — no side-count pops.
- **Harmonic → carousel radius**: 0.177..0.34 of minDim. Floor
  raised at 2.6.2.29 (was 0.12) so petals never collapse too tight.
- **Morph → star**: K-gon ↔ K-pointed star, continuous via
  coronaKgonStar's valley pull-in.

### Phase 3d — trigger shockwave bands (2.6.2.30)

Per-pixel radial brightness bands that sweep over the whole
geometry on each trigger.

- Visadhara.h gains `Internal::triggerCount` (bumped on every
  rising edge) + inline `vizTriggerCount()` accessor. PIMPL state
  only — no SWIG-visible layout change.
- Graphic polls the counter each frame; a change emits a band
  PAIR: an OUTWARD shockwave from center (speed/width from Decay —
  long decay = slow, wide, languid; short = fast snap) and an
  INWARD collapse from beyond the rim (speed/width from Attack's
  bipolar value folded to 0..1 "slowness").
- 8-slot ring buffer → up to 4 trigger pairs in flight at once
  (overlapping pulses). Emission capped at one pair/frame, so
  dense trigger streams become a framerate-bound flicker/strobe —
  intentional, per the user's vision.
- Each band = a raised-cosine bump in normalized-radius space
  (1.0 at center → 0 at ±halfWidth), summed across bands.
- Rendering: base depth shade compressed to 2..9 so bands have
  headroom to flare a line to white. When any band is active each
  K-gon edge is rastered per-pixel (DDA walker + bandModAt); a
  squared-radius annulus reject [gMinR2,gMaxR2] skips the sqrt +
  per-band loop for pixels no band touches. Idle frames (no active
  bands) use the fast fb.line() path.
- `kCoronaBandStrength = 11.0` — a full-strength band adds 11
  levels, clamped at 15.

### Phase 3d.1 — easing + polarity infra (2.6.2.31)

- **coronaEase()**: bands no longer advance `pos` linearly. Each
  carries a linear lifetime progress `t` ∈ [0,1] (stepped by
  `tInc`); `pos` is `lerp(startPos, endPos, coronaEase(t))`.
  coronaEase is quadratic ease-out (`t·(2−t)`) — fast burst,
  decelerating, like a shockwave dissipating. Single namespace
  function = one tunable point to restyle every band.
- **mBandPolarity** (`float`, default +1): the invertibility
  infra for Phase 3e. drawBandLine applies the band as a *signed*
  brightness delta (`baseBright + bandModAt·bandGain`, where
  `bandGain = kCoronaBandStrength · mBandPolarity`) with a 0..15
  dual clamp. +1 = reveal (brighten), −1 = obscure (darken). Held
  as a float so Fold can drive a continuous reveal↔obscure
  crossfade, not just a hard flip. Phase 3e only has to write
  this member.

### Phase 3e — Fold colour inversion (2.6.2.32 → 2.6.2.34)

Fold inverts the whole viz — Fold=0 is bright wireframe on black
with reveal bands; Fold=1 is dark wireframe on white with obscure
bands. Three coordinated moves keyed off `foldPos`:

1. **Background**: `bgBright = foldPos · 15` — pure black field
   at Fold=0, pure white at Fold=1. The old unconditional
   `fb.fill(BLACK)` is removed; the fill happens after Fold is
   read.
2. **Wireframe shade**: per-edge `baseBright` mirrors **within
   the figure's own [2,9] band** — `normalShade = 2 + depthN·7`
   (2..9, front edge bright) at Fold=0 ↔ `invertShade =
   11 − normalShade` (9..2, front edge dark) at Fold=1. The
   figure's STATIC, no-band state visibly inverts (depth shading
   flips) but stays dark, so it's clearly readable against the
   ground at *both* extremes. Staying inside [2,9] also leaves
   the shockwave headroom to push past the base either way.
3. **Band polarity**: `mBandPolarity = 1 − 2·foldPos` → +1
   (reveal) at Fold=0, through 0 at the crossover, to −1
   (obscure) at Fold=1. Feeds `bandGain` (signed-delta infra
   from 3d.1).

Also: drawBandLine's `bright > 0` skip was removed — at Fold>0 a
brightness-0 pixel is the dark figure, not a no-op against black,
so it must be drawn. depthN is clamped to [0,1].

**Version history of this phase:**
- 2.6.2.32 only *dimmed* the figure toward 0..4 instead of
  inverting it; background topped out at 13.
- 2.6.2.33 made figure + background a literal `15 − x` mirror.
- 2.6.2.34 mirrored the figure within its own [2,9] band.
- 2.6.2.32–34 ALL looked broken at Fold=1 for the same hidden
  reason — see below.

**2.6.2.35 — the actual bug: `line`/`fill` BLEND, they don't SET.**
`MainFrameBuffer::line` and `::fill` route through
`blend_pixel_proc`, which is `*frame |= color` — a bitwise-OR
into the 4-bit nibble. They can only turn bits ON. `fb.fill(15)`
at Fold=1 makes a white field, and `fb.line(2..9)` over it is
`15 | x == 15` — the figure literally cannot be drawn. Only
`fb.pixel` (→ `set_pixel_proc`) does a true clear-then-set. That
is exactly why the shockwave bands stayed visible at Fold=1
(drawBandLine uses `fb.pixel`) while the static wireframe and
everything else vanished. Fold=0 worked only by luck: the field
is 0 there, and OR-ing onto 0 behaves like SET.

Fix, two parts:
1. **Background**: `fb.clear(region)` then `fb.fill(bgBright)`.
   The clear zeros the nibbles; the fill ORs bgBright onto 0,
   landing exactly bgBright.
2. **Figure**: the `fb.line` idle fast-path is gone. Every edge
   now rasters through `drawBandLine` (per-pixel `fb.pixel` =
   SET) whether or not a band is active — the only primitive
   that can draw a figure darker than its background. `anyBand`
   removed (the branch it gated is gone).

Mid-Fold is a low-contrast crossover (figure ≈ ground, band
polarity 0) — accepted as a momentary transitional state.

### Phase 3f — optional, deferred

- **V/Oct → tumble speed**: carousel orbit rate from pitch. Not
  done — the Corona viz reads as complete without it, so this was
  left as optional future polish rather than a ship blocker.

## Status: Visadhara shipped at 2.6.2.35 (2026-05-14)

DSP, UI (7 plies + Mode-ply Corona viz), and the full Phase-3
Corona graphic — geometric carousel → continuous Mode/Morph →
trigger shockwave bands → Fold colour inversion — are complete and
on hardware.

Cross-conversation discoveries from this unit, captured in memory:
- `feedback_neon_voice_bus_template` — the full 11-layer NEON
  pattern (2× OS shell, 4-lane×2 voice bus, block-rate bake-in,
  inline/noinline discipline). Took Visadhara to ~26% CPU/instance.
- `feedback_framebuffer_blend_vs_set` — od `line()`/`fill()` are
  bitwise-OR (lighten-only); only `pixel()` SETs. This is what made
  the Fold inversion appear broken for four versions. Plus: the
  0–15 greyscale bright end is perceptually mush — mirror inverted
  figure shades within a dark sub-band, not across the full scale.
