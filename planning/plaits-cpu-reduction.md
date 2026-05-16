# Plaits CPU reduction — full optimization plan

## Premise

Plaits is one of the heaviest units in habitat — single-voice cost is
already noticeable on Cortex-A8 at 48 kHz, and the engine itself
prevents any cross-instance polyphony stacking. Lowering per-instance
CPU is the lever.

We assume (gated by `planning/hexop-fm-unit.md` Phase 0 audition) that
**polynomial sine substitution is acceptable**. That single decision
unlocks NEON across roughly half the engines that were previously
deferred under the no-gather constraint (`feedback_neon_no_gather_lut_dsp.md`).

Even without LUT replacement, the audit identified several engines
that are NEON-viable today (Swarm, Chord, Additive, Modal, Virtual
Analog, Chiptune) — recon notes in `planning/plaits-neon-recon.md`.

This plan is the multi-session execution roadmap. It covers **every**
CPU-reduction avenue: cross-voice NEON, within-voice NEON, SoA filter
banks, block-rate hoist, oversampling tuning, algorithmic substitutions,
and fast-math polynomial libraries. Expect months of intermittent
execution, not a single sprint.

## Scope split with Hexop FM spinoff

Engines **2, 3, 4 (the three 6-op FM engines)** are extracted to the
Hexop FM unit (`planning/hexop-fm-unit.md`). Plaits' 6-op FM engine
slots stay in upstream eurorack code but are de-prioritized — once
Hexop ships, the recommendation will be to use Hexop for FM-heavy work
and Plaits for the other 21 engines.

Engine **15 (Speech)** is excluded: its LUTs are timbre data (LPC
formant tables / phoneme corpus), not approximations of continuous
functions. There's nothing to optimize without re-engineering the
engine, which is out of scope.

Engine **13 (Wavetable)** is excluded from LUT replacement: the
wavetable IS the timbre. NEON applies only to the interpolation math
around the wavetable read; small wins, audit during Phase 4.

## Foundation phase (Phase 0)

Before any engine work, build a shared NEON math library that all
engines pull from.

### `mods/mi/plaits/dsp/neon_math.h` (NEW)

```cpp
// NEON quad math primitives. always_inline, no LUTs, no allocations.
// Lifted/adapted from mods/spreadsheet/visadhara/morph.h and
// mods/spreadsheet/jf/voice.h proven-on-A8 implementations.

float32x4_t sine_poly_4lane(float32x4_t phase01);     // 7th-order odd
float32x4_t cosine_poly_4lane(float32x4_t phase01);   // phase shifted sine
float32x4_t tan_poly_4lane(float32x4_t x);            // small-angle good
float32x4_t exp_poly_4lane(float32x4_t x);            // 5th-order Remez
float32x4_t log_poly_4lane(float32x4_t x);            // 5th-order Remez
float32x4_t wrap01_4(float32x4_t x);                  // fast 0..1 wrap
float32x4_t softclip_poly_4lane(float32x4_t x);       // tanh substitute
```

**Accuracy targets** (audition-gated):

- sine/cosine: −90 dB max error
- tan: −60 dB across [-π/3, π/3]; outside that range fall back to scalar (rare)
- exp/log: −60 dB across audio-relevant range
- softclip: smooth, monotonic, asymptotic to ±1; doesn't need bit-precision

### `mods/mi/plaits/dsp/neon_bus.h` (NEW)

Per-engine SoA layout helpers. Patterns lifted from Visadhara/JF — class-member
scratch arrays, `__attribute__((always_inline))` shape helpers, block-rate
parameter bake helpers, `noinline` secondary workload guards.

### Build-system additions

- File-level `#pragma GCC optimize("no-tree-vectorize")` in any new
  `.cc` that uses NEON intrinsics + SoA init code, per
  `feedback_neon_hint_surfaces`.
- `tools/check-neon-hints.sh` extended to scan the full plaits libmi
  build output, not just specific .o files.

### Foundation verification

- Polynomial sine A/B against `lut_sine` across the audio band on
  representative signals (pure tone, FM modulated tone, swept additive).
  Documented in this file.
- NEON kernel sanity on a synthetic 4-voice oscillator bank — compare
  scalar reference output to NEON output, RMS error within 1e-5.
- objdump on `neon_math.h`-consuming TU: no `vld1.* :64` hints.

## Per-engine ranking

Ratings: **win-per-effort**. HIGH = clean fit, big payoff. MED = some
restructuring needed. LOW = either small payoff or risky.

| # | Engine | LUT-bound? | NEON viability | Effort | Priority |
|---|---|---|---|---|---|
| 16 | Swarm (8 osc unison) | No (poly oscs) | HIGH — pure cross-osc 4-lane | LOW | **P1** |
| 14 | Chord (4-note stacks) | No | HIGH — 4 voices in a quad | LOW | **P1** |
| 12 | Additive (16 partials) | Sine — needs poly sub | HIGH — 4 partials per quad | MED | **P1** |
| 20 | Modal (resonator) | No (already SVF) | HIGH — clone Rings modal pattern | MED | **P1** |
| 0/8 | Virtual Analog (3 osc) | No (BLEP saw/square) | HIGH — within-voice 4-lane on osc bank | LOW | **P2** |
| 7 | Chiptune (NES-style) | No | HIGH — 4 voice channels in a quad | LOW | **P2** |
| 10 | FM (classic 2-op) | Sine — needs poly sub | MED — 2 ops serial; within-instance limited to 2× SIMD | MED | **P3** |
| 1 | Phase distortion | Sine — needs poly sub | MED — single-voice, but waveshape math vectorizes | MED | **P3** |
| 9 | Waveshaping | Waveshape LUT | MED — replace WS LUT with poly; per-sample 4-lane on osc chain | MED | **P3** |
| 19 | String (Karplus-Strong) | No (FIR/IIR + delay) | MED — 3-pass delay-gather pattern | HIGH | **P4** |
| 5 | Wave terrain | Terrain LUT (timbre) | LOW — terrain interp can NEON; small win | MED | **P4** |
| 6 | String machine | No (oscs + filter) | MED — multiple ensemble oscs vectorize | MED | **P4** |
| 11 | Grain | Sample LUT (timbre) | LOW — gather-bound on grain reads | HIGH | **P5** |
| 18 | Particle | Sample LUT (timbre) | LOW — gather-bound; some math vectorizes | HIGH | **P5** |
| 17 | Noise | No | LOW — already cheap; small absolute win | LOW | **P5** |
| 21 | Bass drum | No | MED — small impulse + filter, low absolute cost | LOW | **P5** |
| 22 | Snare drum | No | MED — similar to bass | LOW | **P5** |
| 23 | Hi-hat | No | MED — multi-osc beating; some 4-lane fit | LOW | **P5** |
| 13 | Wavetable | Wavetable (timbre — NO substitution) | LOW — interp math only, 5–10% kernel | MED | **P6** |
| 2/3/4 | 6-op FM | Sine LUT | **EXTRACTED to Hexop** | — | — |
| 15 | Speech | Formant LUTs (timbre — NO substitution) | **EXCLUDED** | — | — |

Priorities P1–P6 group engines into execution waves. Each priority
wave is one or two ER-301 release cycles (PKGVERSION bumps) of work.

## Phase 1 — Quick wins (P1 engines)

### 1a. Swarm (engine 16)

**File**: `eurorack/plaits/dsp/engine/swarm_engine.cc`

Swarm has 8 unison oscillators per output channel. Layout state SoA
(`phase[8]`, `phase_inc[8]`, `detune[8]`, etc.), inner loop processes
4 oscillators per NEON iteration (× 2 for the 8 oscs).

Estimated win: ~40 % engine CPU reduction at full unison density.

### 1b. Chord (engine 14)

**File**: `eurorack/plaits/dsp/engine/chord_engine.cc`

4 chord voices map directly to one NEON quad. Each voice's per-sample
work (oscillator + envelope + amp) runs lane-parallel.

Estimated win: ~50 % engine CPU reduction.

### 1c. Additive (engine 12)

**File**: `eurorack/plaits/dsp/engine/additive_engine.cc`

16 partials → 4 partials per NEON iteration. Requires polynomial
sine (Phase 0 foundation). Phase increments + amplitudes are SoA.

Estimated win: ~50–60 % engine CPU reduction. Big absolute win since
additive is the most expensive non-FM engine.

### 1d. Modal (engine 20)

**File**: `eurorack/plaits/dsp/engine/modal_engine.cc`

Already SVF-based; clone the `mods/mi/rings/dsp/resonator.cc` SoA
NEON kernel directly. ~20 modes, pad to 24 (6 quads).

Estimated win: ~50 % engine CPU reduction (matches the Rings modal
result, since the math is identical).

### Phase 1 verification

- NEON hint audit on each modified .o
- Engine-by-engine A/B with the unmodified upstream (RMS within 1e-4
  on a swept-noise test signal)
- Hardware audition per engine: representative patches, no audible
  regression
- PKGVERSION bump after each engine ships

## Phase 2 — VA family + Chiptune (P2 engines)

### 2a. Virtual Analog (engines 0, 8)

**File**: `eurorack/plaits/dsp/engine/virtual_analog_engine.cc`

3-oscillator VA (saw + square + sub). The two engines share the
`AnalogOscillator` class. Within-voice 3 oscs → pad to 4 → NEON
quad. BLEP impulse generation vectorizes cleanly (it's polynomial,
not LUT-based).

Estimated win: ~30–40 % engine CPU reduction.

### 2b. Chiptune (engine 7)

**File**: `eurorack/plaits/dsp/engine/chiptune_engine.cc`

NES-style 4-voice (2 pulse + 1 triangle + 1 noise). Map the 4 voice
channels onto a NEON quad. Each voice's per-sample work is small
polynomial waveform generation.

Estimated win: ~50 % engine CPU reduction.

## Phase 3 — LUT replacement engines (P3)

**Gate**: Phase 0 polynomial sine audition + per-engine audition.
This phase is the FM-character-critical work.

### 3a. Classic 2-op FM (engine 10)

**File**: `eurorack/plaits/dsp/engine/fm_engine.cc`

Replace `lut_sin` reads with polynomial. 2 ops serial within instance,
but the *operator math* vectorizes via 4× oversampling lanes
(operate on 4 OS-samples in parallel per output sample). Already
has 4× OS, so re-using the OS dimension as the SIMD axis is free.

Estimated win: ~30–40 % engine CPU.

### 3b. Phase distortion (engine 1)

**File**: `eurorack/plaits/dsp/engine/phase_distortion_engine.cc`

Replace `lut_sin` + the phase-distortion shape LUT with polynomials.
Engine is mono so cross-voice NEON N/A; gains come from removing
LUT scalar costs and inlining the distortion math.

Estimated win: ~15–20 % engine CPU.

### 3c. Waveshaping (engine 9)

**File**: `eurorack/plaits/dsp/engine/waveshaping_engine.cc`

Replace the `lut_bipolar_fold` waveshaper with a polynomial fold
function (smooth, symmetric, periodic). Wave-and-fold math is
already vectorization-friendly; LUT was the only blocker.

Estimated win: ~25–35 % engine CPU.

## Phase 4 — Delay / physical-model engines (P4)

### 4a. String engine (engine 19)

**File**: `eurorack/plaits/dsp/engine/string_engine.cc`

Karplus-Strong. Apply the **3-pass delay-gather pattern**
(`feedback_neon_delay_gather`): isolate the scalar delay-read pass
with deep prefetch, vectorize the FIR/IIR filter pass and the
combine pass. Similar structure to what was scoped for Rings String
mode but never executed.

Estimated win: ~25–35 % engine CPU. Highest risk in this phase —
the gather pass needs careful prefetch tuning.

### 4b. String machine (engine 6)

**File**: `eurorack/plaits/dsp/engine/string_machine_engine.cc`

Ensemble of detuned saw oscillators through a paraphonic filter.
Multiple oscs vectorize cross-osc; the chorus stage's modulated
delays use the same 3-pass pattern as 4a.

Estimated win: ~25–30 % engine CPU.

### 4c. Wave terrain (engine 5)

**File**: `eurorack/plaits/dsp/engine/wave_terrain_engine.cc`

Terrain is a small 2D LUT (timbre data). Math around the terrain
lookup (warping, panning, scanning) vectorizes; the lookup itself
stays scalar. Small win.

Estimated win: ~10–15 % engine CPU.

## Phase 5 — Granular + drums + noise (P5 engines)

### 5a. Grain (11) + Particle (18)

Sample-LUT-bound engines. The grain envelopes + scheduling vectorize;
the sample reads stay scalar (gather-bound). Estimated wins are
modest, ~10–15 %. Lower priority — high engineering effort, small
payoff.

### 5b. Drum engines (21, 22, 23)

Bass / snare / hi-hat. Each has a small per-instance CPU footprint.
Aggregating optimizations across all three may yield perceptible
relief in a drum-heavy patch. Estimated: ~20–30 % per engine, but
small absolute numbers.

### 5c. Noise (17)

Already lightweight. Skip unless evidence emerges that it's a hot
path in a specific user patch.

## Phase 6 — Wavetable interpolation (P6 engine)

### 6a. Wavetable (engine 13)

**File**: `eurorack/plaits/dsp/engine/wavetable_engine.cc`

Wavetable lookups stay scalar (timbre data). Interpolation math
(linear / cubic between samples, frame crossfading, anti-aliasing
filter) vectorizes within a single sample read.

Estimated win: ~10–15 % engine CPU. Worth doing because Wavetable
is heavily used, but the absolute win is smaller than P1–P3.

## Cross-cutting optimizations (apply to all phases)

### CCO-1: Block-rate parameter hoist

Many engines run parameter interpolation per-sample. Where the
audible difference is below the slew threshold, hoist to block-rate
(every 16 or 32 samples). Visadhara's bake-in pattern is the
reference. Per-engine audition gate.

### CCO-2: `always_inline` on shape helpers

Force inline on small NEON helper functions (sine, wrap01, softclip).
Prevents register spills across function calls per
`feedback_neon_hint_surfaces`.

### CCO-3: Branchless mode dispatch

Replace `switch (mode_) { case X: ... case Y: ... }` runtime branches
in DSP inner loops with per-mode gain coefficients baked at block
rate. Per `feedback_runtime_branched_dsp_dispatch` — runtime branches
in DSP can hard-fault Cortex-A8 under `-O3 -ffast-math`. Branchless
arithmetic masking is the safe pattern *and* faster.

### CCO-4: Oversampling audit

Each engine has its own OS configuration. Audit per-engine:

- Engines with 4× OS that audibly work at 2× → drop (saves 50 % of
  that engine's CPU)
- Engines with no OS that audibly need it → add (cost up-front,
  fixes audible artifacts)

The classic 2-op FM already has 4× OS. Wavetable / VA at 4×. Some
drum engines at 1×. Each is its own audition decision.

### CCO-5: SoA conversion of stateful filter banks

Where an engine has a bank of N filters (Modal, Chord chorus,
Additive partials), convert state arrays from AoS to SoA so NEON
loads are contiguous. Pattern in `feedback_neon_soa_svf_bank`.

### CCO-6: Auto-vec init trap prevention

File-level `#pragma GCC optimize("no-tree-vectorize")` on every new
`.cc` that has NEON intrinsics next to SoA init/constructor code.
Per `feedback_neon_hint_surfaces`. Hand-written NEON is not
affected; this pragma only kills the auto-vectorizer.

## Build / packaging

- PKGVERSION bump per shipped engine, per `feedback_package_version_bump`.
- Auto-install Linux builds to emu, per `feedback_linux_build_auto_install`.
- SWIG wrapper force-clean if any wrapped header changes, per
  `feedback_swig_header_dep`.

## Verification gates (per engine)

Each engine shipping its own NEON pass must clear:

1. **Build**: both `ARCH=linux` and `ARCH=am335x` clean.
2. **Hint audit**: `tools/check-neon-hints.sh` on the modified .o —
   zero new SUSPECT hints beyond pre-existing baseline.
3. **Numerical**: emu A/B against pre-change build on swept-noise
   input, RMS within 1e-4.
4. **Audible**: hardware audition on representative patches.
   Acceptance: no perceived regression on patches the engine is
   known for.
5. **CPU**: hardware CPU drops by at least 60 % of the estimated
   win (i.e., if estimate is 50 % reduction, accept ≥30 % measured).
6. **Commit**: with PKGVERSION bump and audit-doc entry.

## Out of scope

- Plaits 6-op FM (extracted to Hexop)
- Speech engine (timbre LUTs, no substitution path)
- Re-architecting Plaits' upstream macro-control / morph system —
  that's a separate UX project.

## Bookkeeping

This plan lives across multiple ER-301 release cycles. Each shipped
engine adds a one-line entry to `planning/neon-opportunities.md`
(audit doc) marking that engine "done — see commit X". The
recon `planning/plaits-neon-recon.md` stays as the per-engine
analytical reference; this doc is the execution plan.

## Cross-references

- `planning/hexop-fm-unit.md` — the 6-op FM spinoff
- `planning/plaits-neon-recon.md` — recon analysis (read-only reference)
- `planning/neon-opportunities.md` — audit doc (updated per phase)
- `feedback_neon_voice_bus_template` — cross-voice NEON contract
- `feedback_neon_soa_svf_bank` — filter-bank SoA pattern
- `feedback_neon_delay_gather` — 3-pass delay pattern
- `feedback_neon_no_gather_lut_dsp` — the no-gather constraint
- `feedback_neon_intrinsics_drumvoice` — class-member array rule
- `feedback_neon_hint_surfaces` — auto-vec init trap mitigation
- `feedback_runtime_branched_dsp_dispatch` — branchless dispatch rule
