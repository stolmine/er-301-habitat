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

### Pitch envelope dilution (deferred to next commit)

User also flagged the Liquid mode pitch sweep feels diluted. Three
contributing factors identified for a follow-up commit:

1. 50 ms exponential decay — most of the bend is in the first 20 ms,
   too fast to register perceptually. Target ~150 ms.
2. Symmetric depth across voices — all 6 sweep +1 oct together, so
   the bend reads as one fat voice rather than a cascading layered
   event. Target asymmetric per-voice depth (voice 0 +2 oct → voice 5
   +0.3 oct or zero), giving low-end-heavy bend with each voice
   arriving at target at perceptibly different times.
3. Cross-mode injection during sweep — un-swept Metal bus mixing into
   Skin/Liquid in negative-Attack region thins the sweep. Probably
   orthogonal but worth noting.

Out of scope for the oversampling commit; tracked here so the
follow-up has a starting point.

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
