---
name: Ngoma codex — architecture, audio path, and dev journey
description: Canonical reference for Ngoma (the analog macro drum voice in mods/spreadsheet/). Covers the current DSP architecture, the audio signal flow, the modulator/envelope graph, the optimization wins, and the bisect history that taught this codebase how to use NEON safely. Pulled from the 2.5.5.92 → 2.5.5.115 series of ~25 versions of incremental hardware bisects and sound-design refinement on Cortex-A8 / am335x. Read this before touching DrumVoice.cpp/.h.
type: project
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---
# Ngoma — what it is

Ngoma is a self-contained drum voice in the spreadsheet package. C++ DSP with Lua control wiring; six top-level Pattern A controls (V/Oct, Character, Sweep, Decay, Level, xform Randomize). Several auxiliary controls inside expanded views. The DSP renders mono output (stereo via per-channel duplication) at the framework's 48 kHz sample rate. Inner audio loop is 2× oversampled for FM-rich oscillator paths.

The voice is *drum-like* but not a faithful Trinity Block clone — it has its own opinionated character. Source codebase paths:

- `mods/spreadsheet/DrumVoice.h` — public class, parameter declarations, NEON-friendly class-member arrays
- `mods/spreadsheet/DrumVoice.cpp` — DSP, polynomial helpers, NEON-vectorized phasor banks
- `mods/spreadsheet/assets/DrumVoice.lua` — Lua wiring (op + adapters + ties + topo)
- `mods/spreadsheet/assets/DrumVoiceCharacterControl.lua` — custom Pattern A control with rotating cube graphic
- `mods/spreadsheet/assets/DrumCubeGraphic.{h,cpp}` — 3D cube viz that polls op:getCharacter/Shape/Grit per draw frame
- `mods/spreadsheet/assets/DrumVoice{Pitch,Sweep,Decay,Level}Control.lua` — other custom Pattern A controls

# Architecture

## Sound source bank (in mix order — all summed at osSamp[k] inside the 2× oversample inner loop)

| Source | Frequency | Polynomial / LUT | Mix amount | Shape gate? | Envelope |
|---|---|---|---|---|---|
| Osc1 (carrier) | droopFreq | LUT (character morph) | 1.0 | — | s.ampEnv |
| Osc3 (detuned unison) | droopFreq · 1.004 (~7 cents) | LUT (character morph) | 0.6 | — | s.ampEnv |
| Sub-sine fundamental (lane 2 of mPhaseBank) | currentSubFreq | NEON poly | (1−shape)·0.5 | yes | s.ampEnv |
| 3rd-harmonic partial (lane 3 of mPhaseBank) | 3·droopFreq | NEON poly | (1−shape)·0.25 | yes | s.ampEnv |
| Sub-octave partial (lane 0 of mPartialPhases) | 0.5·currentSubFreq | NEON poly | 0.3 | NO | mPartialEnvs[0] |
| Membrane mode 1 (lane 1 of mPartialPhases) | mode1Ratio·droopFreq | NEON poly | 0.16·(1−shape·0.4) | light | mPartialEnvs[1] |
| Membrane mode 2 (lane 2 of mPartialPhases) | mode2Ratio·droopFreq | NEON poly | 0.13·(1−shape·0.4) | light | mPartialEnvs[2] |
| Membrane mode 3 (lane 3 of mPartialPhases) | mode3Ratio·droopFreq | NEON poly | 0.10·(1−shape·0.4) | light | mPartialEnvs[3] |
| SVF noise (post-decimation) | broadband | LP→BP morph by pitch | (grit−0.4)·2.5 + aboveKnee·2.0 | — | s.ampEnv |

The pitch-morph for membrane modes (block-rate, log-pitch interpolation):

| baseFreq | mode1 | mode2 | mode3 | feel |
|---|---|---|---|---|
| 55 Hz | 2.5× | 4.2× | 7.0× | wide spread (sub-bass enrichment) |
| 110 Hz | ~2.2× | ~3.5× | ~5.5× | snare body modes |
| 220 Hz | ~1.9× | ~2.7× | ~3.9× | tom/snare crossover |
| 440 Hz | 1.7× | 2.0× | 2.4× | tight cluster (cymbal sheen) |

## Modulator graph (FM into Osc1 + Osc3)

- **Shape modulator (osc2)** — triangle/character-morph at `droopFreq · (1 + shape·0.0058)`. Scalar LUT sine. FM index = `shape² · shapeEnv · 6` (+ grit boost).
- **Spacious FM modulator (lane 1 of mPhaseBank)** — sine at `2·droopFreq`, NEON poly. Active above shape > 0.5; depth `(shape − 0.5) · 2 · shapeEnv · 3`.
- **Metallic FM modulator (lane 0 of mPhaseBank)** — sine at `2.71·droopFreq`, NEON poly. Inharmonic ratio for 808-clang spectra. Depth = `grit · 2 · ampEnv`.
- **Grit noise FM** — direct broadband noise injection into osc1 phase increment when grit > 0. Depth = `grit² · 1000 · ampEnv` (+ character boost).

All four FM contributions are summed into both osc1 and osc3 phase increments (osc3 scaled by 1.004 for the detune).

## Envelope graph

| Envelope | Rate | Source/derivation | Purpose |
|---|---|---|---|
| s.ampEnv | per-sample | attack/hold/decay state machine | master amplitude; gates everything |
| s.shapeEnv | per-sample | decay coeff × ampEnv coeff (0.6×) | shape FM depth (faster fade than amp) |
| s.punchEnv | per-sample | 3 ms exponential decay | onset transient boost (1 + punchEnv multiply) |
| mPartialEnvs[0..3] | per-sample (NEON quad) | per-partial coeffs computed at trigger | sub-octave + 3 mode partials decay independently, **scaled by pitch register** (kick = short, cymbal = long) |
| s.wobblePhase + LFO | per-sample | LUT sine, decay-scaled rate | pitch-droop wobble (composite of decay+sweep+pitch) |

## Pitch sources

- **currentFreq** — main pitch-sweep tracker. Trigger sets `freqStart = baseFreq · 2^(sweep/12)`, converges to `baseFreq` over `sweepTime · sr · 0.7` samples (1.4× faster than the knob suggests, by design). Per-sample geometric step via `s.sweepRatio`.
- **currentSubFreq** — gentler sub-sweep at 0.3× sweep amount. Used for sub-sine and sub-octave partial — fundamental retains pitch identity longer than the carrier.
- **droopFreq** — per-sample `currentFreq · (1 + ampEnv·0.015 + wobble) · (1 + punchEnv·0.05)`. Static droop + decay-tied wobble LFO + transient punch droop.

## Output chain (post k-loop)

1. Sum `osSamp[k]` from all sources at 2× rate
2. 2-tap MA decimator: `sample = 0.5 · (osSamp[0] + osSamp[1])`
3. **Grit-knee attenuation**: `sample *= (1 − grit·0.3 − aboveKnee·2.0)` where aboveKnee = max(0, grit − 0.7). Oscs collapse to ~0.10 at grit=1.
4. **SVF noise mix**: pitch-tracked LP→BP filter on broadband noise, mixed with `(grit − 0.4)·2.5 + aboveKnee·2.0` gain. At grit=1 noise dominates.
5. `sample *= s.ampEnv`
6. **Punch boost**: `sample *= (1 + punchEnv)` (3 ms decay, scaled at trigger by `1 + aboveKnee·2`).
7. Optional clipper (tanh with gain comp), enabled when Clipper > 0.
8. Optional EQ (TPT SVF, bipolar LP/HP), enabled when |EQ| > 0.01.
9. Optional one-knob compressor (CPR pattern from Larets), enabled when CompAmt > 0.001.
10. `out[i] = sample · level`

## NEON state — class members (heap-allocated, no alignment annotation)

Two 4-lane NEON quads, both as `float[4]` class members of DrumVoice (NOT inside the Internal struct, NOT stack-locals):

```cpp
// Bank 1 — pure-sine modulators + 3rd harmonic
float mPhaseBank[4];     // [phaseFm, phase4, phase5, phase3rd]
float mIncBank[4];
float mSineBank[4];

// Bank 2 — additive partials with per-partial envelopes
float mPartialPhases[4]; // [subOctave, mode1, mode2, mode3]
float mPartialInc[4];
float mPartialSines[4];
float mPartialEnvs[4];   // NEON-decayed per output sample
float mPartialDecayCoeffs[4]; // computed at trigger via expf
```

Each k-iter calls `neonAdvanceSines(phases, inc, sines)` twice (once per bank) — phase advance + wrap + triangle + 7th-order polynomial sine on all 4 lanes in parallel.

# The bisect journey (~25 versions of hardware lessons)

This unit went through unusually painful hardware-only crash bisects. Treating future Ngoma changes with the discipline these revealed will save days.

## 2.5.5.92–.97 — runtime-branched DSP dispatch crashes Cortex-A8

`switch(target)` / `if(target<=N)` chains / function-pointer tables with **differential case bodies** on a runtime tier value all crashed hardware (loaded fine in emu). Memory: `feedback_runtime_branched_dsp_dispatch.md`.

**Resolution (.98)**: single method + **branchless arithmetic masking**. Always do all 10 randomization writes; mask out tier-skipped writes by setting their `depth=0 AND spread=0`, which makes the underlying `randomizeValue` return `cur` unchanged. Ternaries (`(target <= N) ? depth : 0.0f`) compile to CMP+MOVCC on Cortex-A8 — no control flow branch.

## 2.5.5.105–.111 — NEON intrinsics in DrumVoice need class-member storage

First NEON migration of pure-sine paths froze hardware. Bisect via inert ops, stack-locals with/without `alignas(16)`, class members. Memory: `feedback_neon_intrinsics_drumvoice.md`.

**Root cause**: GCC `-O3 -ffast-math` on stack-local `float[4]` arrays (with or without alignment annotations) emits `vld1.32 {q8}, [reg :64]` strict-aligned-hint instructions. AAPCS only guarantees 8-byte stack base alignment, so the runtime address fails the `:64` hint and Cortex-A8 NEON raises a Data Abort. Confirmed via `arm-none-eabi-objdump`.

**Resolution**: NEON-touched arrays must be **class members or heap-allocated**. Class members get `vld1.32 [reg]` (no hint) which Cortex-A8 NEON HW handles regardless of alignment. **Don't use `alignas(16)` or `__attribute__((aligned(16)))` on stack locals destined for NEON.** Pecto, Clouds, Rings all follow this pattern.

**Verification recipe** before installing:
```
arm-none-eabi-objdump -d testing/am335x/mods/spreadsheet/DrumVoice.o \
  | awk '/<_ZN8stolmine9DrumVoice7processEv>/,/^Disassembly/' \
  | grep -E "vld1\.32|vst1\.32"
```
All 128-bit `{d18-d19}`/`{d20-d21}`/`{d16-d17}` quad ops should show `[reg]` (no hint). `[reg :64]` on single-D-register stores to `[sp]` is fine (AAPCS guarantees 8-byte stack alignment).

## Other lessons baked in

- `feedback_comparator_gate_threshold.md` — xformGate uses `> 0.5f` rising-edge detection (0.0 trips on uninitialized fuzz, hangs)
- `feedback_package_trig_lut.md` — `sinf`/`cosf` from package `.so` miscompute on hardware. Polynomial sine + LUT-based sin via `lookupSine(s.sLUT, ...)` sidestep this entirely.
- `feedback_package_version_bump.md` — 4th-digit dev iteration only; bump the bottom segment for every rebuild.

## Post-codex bisect chase (2026-04-27 / 04-28)

Hardware lockup-on-insertion regression. Working state was 2.5.5.119 per codex; current state crashes. Walked through every memory-cataloged classic culprit, fixes shipped:

- **2.5.5.165**: constructor `:64` hint fix. Discovered gcc auto-vectorizes the synthesized member-init of 14 contiguous `od::Parameter *mBias* = nullptr` fields in `DrumVoice.h` into 4 quad-D `vst1.64 :64` stores — the trap-prone pattern. Function-level `optimize("O0")` attribute didn't reach the synthesized init code. Fix: drop in-class `= nullptr`, single memset in constructor body. Constructor :64 hints went 4 → 0. **Crash still repros, "takes slightly longer"** (got past constructor but tripped elsewhere).
- **2.5.5.166**: explored process() `:64` register-pressure spills. 3 hints inherent to gcc's spill strategy for the per-sample NEON intrinsic loop (4-lane phasor + sine quad + partial-env decay). Persist at -O3 / -O2 / -O1 / -no-tree-vectorize alike. Would need restructure to factor inner loop into a noinline helper with separate register window.
- **2.5.5.167**: trigger threshold `> 0.1f` → `> 0.5f`. 0.1 trips on uninitialized inlet fuzz at insertion. Crash still repros.
- **2.5.5.168**: applyRandomize xform target dispatch — RE-DISCOVERED THE .92-.97 TRAP. The 117-119 `-env` tier (target=2) made env-lock and oct-lock orthogonal. Original .98 monotonic single-comparison ternaries (`target <= 2 ? depth : 0`) became boolean expressions like `(target != 2) && (target != 4) ? depth : 0` — gcc compiled into actual `bgt`/`ble` branches in `applyRandomize` body. Verified via objdump. Fix: table-indexed mask lookup (`kEnvOnMask[target]`) — branchless via load. Disassembly confirmed dispatch is now `lsl r3, r5, #2; ldr` (single load, no branch). Crash still repros.
- **2.5.5.169** (BISECT BASELINE): DrumVoice.cpp + DrumVoice.h reverted verbatim to 0dd8870 (`.116`). Current SDK + build infrastructure. **Hardware: still crashes on insertion (2026-04-28).** Confirms bug is in SDK / shared-infra, not Ngoma source. Tier 2 (`tools/check-neon-hints.sh`) on `.169`'s DrumVoice.o flagged 4 SUSPECT ctor `:64` hints (`vst1.64 {d16}, [reg :64]` at `this+0x4C0/4D0/4E0/4F0`) — the same auto-vectorized member-init surface `.165` had cleared. Reverting to `.116` source reintroduced them. Plus 1 SUSPECT process() `:64` hint at 0xc20 (register-spill, indirect via sp+152). Probe #1 (parent class layout diff): NEGATIVE — parallel-DSP MVP commits don't touch DrumVoice's ancestor chain.

**Cross-arch confirmation**: crash does NOT reproduce on RPi 4 / Cortex-A72 / aarch64 emu (`docs/dev-rig-procedures.md`, build-flag fix in commit `3c59484`). Trap is am335x-specific.

**Diagnostic split for 169**:
- 169 still crashes → SDK / shared-infra mismatch. Most likely candidate: parallel-DSP MVP commits (`378a78a` Phase 5, `262579d` Phase 7, `c1a6930` Phase 7 fix) extended `od/tasks/UnitChain.h` with new private fields (`mIsChannelChain`, `mLoadEwmaPct`, `mInProcess`, `mPendingRemovals`). Hardware firmware built before those commits would have a smaller UnitChain layout — package built against current habitat SDK has ABI mismatch.
- 169 works → bug is in 117-119+ Ngoma source. Re-bisect within 0dd8870..e65584e..23291ef.

**New rule for next-time**: when adding tier values to the xform target dispatch, the new tier scheme MUST preserve monotonic single-comparison conditions — anything orthogonal forces boolean conjunctions which compile to real branches and re-trip the .92-.97 trap. Either keep the chain monotonic by reordering, or use table-indexed mask lookups from the start (the .168 fix pattern).

**Investigation cost**: 5 versions (.165-.169), full classic-culprit walkthrough, RPi dev-rig setup. Confirms am335x is brittle to NEON `:64` hints and to runtime-branched DSP dispatch beyond what straightforward C++ can express.

# Optimization wins over the project

| Version | Change | CPU |
|---|---|---|
| 2.5.5.106 | Scalar polynomial sine for 3 pure-sine paths | -1% |
| 2.5.5.111 | NEON 4-lane phasor + polynomial sine quad | -6% |
| 2.5.5.113 | Second NEON quad for sub-octave partial | +1% |
| 2.5.5.115 | Per-partial NEON env decay quad | ~+0.5% |

Net from baseline: ~30% CPU on Cortex-A8 with all features active. Plenty of headroom for additional partials in unused lanes if needed.

# Patterns this unit uses (post-bisect canonical forms)

## Branchless tier behavior

Anywhere a runtime tier value would normally trigger `switch`-with-different-bodies, use **all-tiers-do-everything + mask coefficients to no-op**. Example from `applyRandomize`:

```cpp
bool envOn   = (target <= 2);
float depthEnv  = envOn  ? depth  : 0.0f;
float spreadEnv = envOn  ? spread : 0.0f;
// ...always call doRnd for every param; depth=0+spread=0 makes it a no-op
```

## NEON working memory

```cpp
// In header (private):
float mPhaseBank[4];      // class member, heap-allocated via new
float mIncBank[4];
float mSineBank[4];

// In process(), inside k-loop:
mIncBank[0] = ...;        // populate from runtime values
neonAdvanceSines(mPhaseBank, mIncBank, mSineBank);
float lane0 = mSineBank[0];  // extract via plain array access
```

Never:
- `alignas(16) float buf[4]` for NEON
- `__attribute__((aligned(16))) float buf[4]` for NEON
- Stack-local `float buf[4] = {1.0f, 2.0f, 3.0f, 4.0f}` then NEON-load
- Member arrays of an Internal struct that's only allocated within a single .cpp (class members of the public class are safer)

## Polynomial sine helpers

Two flavors:

```cpp
// Scalar: 7th-order odd-power, max error ~7e-6, bounded to ~0.99988
static inline float polySine(float tri);

// NEON 4-lane: same polynomial, computes 4 sines per quad call
static inline void neonAdvanceSines(float *phases, const float *inc, float *outSines);
```

The NEON helper has both NEON (`#ifdef __ARM_NEON`) and scalar fallback paths. **Same polynomial coefficients**, so emu and hardware produce identical sines.

## Per-partial decay envelopes (NEON-vectorized)

```cpp
// Trigger time: compute coefficients
mPartialDecayCoeffs[0] = expf(-1.0f / (decay * sr));
// ...etc for each lane

// Per output sample: NEON quad multiply (1 vld + 1 vmul + 1 vst)
float32x4_t pe = vld1q_f32(mPartialEnvs);
float32x4_t pc = vld1q_f32(mPartialDecayCoeffs);
pe = vmulq_f32(pe, pc);
vst1q_f32(mPartialEnvs, pe);

// Mix scaling uses per-partial env instead of s.ampEnv
float mode1Mix = 0.16f * mPartialEnvs[1] * shapeGate;
```

# Working on Ngoma — checklist

Before any DSP change:

1. **Read this codex first**. Then re-read the related `feedback_*` memories for the area you're touching.
2. **Bisect everything**. The unit has a documented history of crashes that work in emu. Probe with the smallest possible change. Verify on hardware before stacking the next change.
3. **Verify NEON via objdump** before installing if you touched NEON code. Look for `vld1.32 [reg]` (no hint) on quad operations.
4. **Branchless masks for runtime-tier behavior**, not switch-with-different-bodies.
5. **Class members for NEON working memory**, not stack-locals.
6. **Bump 4th-digit version per rebuild**, force re-extraction of the rear-SD package.
7. **Polynomial sine for pure-tone paths**; LUT sine for character-morph paths.
8. **Comparator threshold = 0.5f** for rising-edge detection on Comparator-driven inlets.

# Sound-design knobs

These are tuneable without architectural change. Edit, version-bump, listen.

- `subOctaveMix` constant (current 0.3) — bass weight
- `partial3Mix` constant (current 0.25) — high harmonic sheen
- `mode1Mix/2/3` constants (current 0.16/0.13/0.10) — membrane mode mix balance
- `pitchDecayScale` formula (current `0.5 + pitchParam * 1.5`) — pitch register decay scaling
- `mode1Ratio/2/3` interpolation endpoints (current 2.5→1.7, 4.2→2.0, 7.0→2.4) — partial spread morph
- `pitchParam` log-pitch range (current 55Hz to 440Hz, 3 octaves)
- Wobble depth/rate composite formulas
- Grit-knee threshold (currently 0.7)
- Sub-sine sweep scale (currently 0.3× of main sweep)

Try in this order when iterating sound: mix levels → ratios → envelope timing → modulator depths → pitch ranges.
