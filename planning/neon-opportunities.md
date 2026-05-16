# NEON optimization opportunities (habitat)

**Audit date: 2026-05-14. Updated: 2026-05-15.** No prior dedicated
NEON planning doc existed — NEON references were scattered across
other planning docs (`network-*`, `jf-initial-pass`,
`alembic-phase-8`, `visadhara-initial-pass`, etc.) and several
feedback memories. This consolidates the picture into a single
matrix with prioritization principles informed by the Visadhara and
Ngoma campaigns.

## Current state (2026-05-15)

**Shared foundation now exists**: `mods/spreadsheet/util/neon_math.h`
provides `log2_poly_4lane`, `exp2_poly_4lane`, `sine_poly_4lane`,
`wrap01_4` with scalar fallbacks. Shipped with the multiband arc.
Any new NEON pass in spreadsheet (and easily adapted for other
packages) should pull from this header rather than re-deriving
polynomial primitives.

**In flight / queued**:
- `planning/plaits-cpu-reduction.md` — broad multi-phase plan for 21
  Plaits engines. Phase 0 (foundation library) shipped with multiband.
  Phase 1 targets (Swarm, Chord, Additive, Modal) ready to start.
- `planning/hexop-fm-unit.md` — separate spinoff: extract Plaits 6-op
  FM as polyphonic NEON voice on tomf-polygon bus. Phase 0 is a hard
  audition gate (polynomial-sine vs LUT for FM tone).

**Recently shipped**:
- Filterbank (spreadsheet 2.6.2.42) — SoA SVF bank NEON
- Rings modal (mi 1.0.2) — single-accumulator quad
- Multiband units (spreadsheet 2.6.2.43-46) — Impasto + Parfait
  per-band SoA NEON + fast paths. Impasto 27%→13-15%, Parfait
  35%→25-27%
- Petrichor (spreadsheet 2.6.2.47) — 3-pass restructure + SoA SVF
  bank NEON. 55%→30% at 8 taps + filters

## Prioritization principles (recent lessons baked in)

### 1. Architectural before NEON

The Visadhara Corona Fold contour field caused encoder capture in
2.6.2.38. NEON of the per-pixel `Σsqrt + Σcos` would have given ~4×;
the angle-subtraction precompute cache (2.6.2.39) gave ~50× — the
expensive work was *frame-invariant except for a global phase
offset*. Per `feedback_viz_encoder_capture_architectural`, viz UI
lag is "almost always draw-path structure, not CPU." The same logic
applies to DSP hot loops: ask "is this work actually per-sample, or
is it being recomputed when it doesn't need to be?" before
vectorizing. NEON of a redundant computation is still redundant.

### 2. Don't NEON release-stable units without a CPU reason

Ngoma is the reference. After the .175 release-stable cut, an audit
(2026-05-14) found 1 SUSPECT non-sp `:64` hint in `DrumVoice::process()`
— down from the codex-era ~16, working, no CPU pressure. Decision:
leave it alone. NEON has a documented history of hardware-only
crashes on this codebase (`feedback_neon_intrinsics_drumvoice`,
`feedback_neon_hint_surfaces`, the .165–.169 bisect chase) — the
price of a touch is real. Don't trade working hardware for cleaner
codegen.

### 3. Apply the voice-bus template — don't reinvent

`feedback_neon_voice_bus_template` captures the proven 11-layer
pattern from Visadhara (2× OS shell, class-member float[8] storage,
block-rate bake-in, named-broadcast pass scopes, `always_inline`
shape helpers, `noinline` secondary workloads, unpacked-scalar
args, pre-multiply across-call quads, coefficient pre-packing,
`wrap01_4`, verification). Took Visadhara from 2× CPU post-OS down
to ~26% per instance. Reuse for any unit shaped like "N ≤ 8
parallel voice/op states summed per sample."

### 4. Apply the delay-gather template for multitaps

`feedback_neon_delay_gather` captures the 3-pass
compute/gather/combine pattern from Pecto stereo (50% → ~6% CPU).
Applies to any multitap delay with N taps summed per sample with
serial read-pointer dependence.

### 5. No NEON in the graphics path (yet)

Verified 2026-05-14: **no graphics file in any habitat package uses
NEON.** Helicase / Ngoma / Visadhara NEON is entirely DSP-side. A
first graphics-path NEON entry would need extra care — graphic
headers compile into `spreadsheet_swig.o` at `-Os`, not the DSP
path's `-O3 -ffast-math`, so the `:64` trap behaviour there is
unproven. And per Principle 1, an architectural fix (cache,
time-slicing, dirty regions) almost always beats vectorizing a viz
draw anyway.

## Reference patterns

| Pattern | Memory | Where applied | Win |
|---|---|---|---|
| 11-layer voice bus + 2× OS | `feedback_neon_voice_bus_template` | Visadhara | 2× post-OS → ~26% |
| 3-pass multitap gather | `feedback_neon_delay_gather` | Pecto stereo | 50% → ~6% |
| Class-member NEON storage | `feedback_neon_intrinsics_drumvoice` | Ngoma + all NEON units | trap avoidance |
| Hint audit pre-install | `feedback_neon_hint_surfaces` + `tools/check-neon-hints.sh` | mandatory | trap detection |

## Audit matrix — DSP units

State per grep of NEON intrinsics in `.cpp` / `.h`, 2026-05-14.

### spreadsheet/

| Unit | NEON state | Opportunity | Priority | Notes |
|---|---|---|---|---|
| **Visadhara** | Full template (`Visadhara.h`) | none — the reference | DONE | template origin |
| **Pecto** | Full delay-gather (`Pecto.cpp`) | none — the reference | DONE | delay-gather origin |
| **Network** | Heavy NEON (`Network.h`, 57 hits) | none for now | DONE | V1 parallel-multitap restored at 2.6.2.0; tuned through .2.6.1.84 |
| **Ngoma (DrumVoice)** | Partial (`DrumVoice.cpp`) | template re-pass for the 1 SUSPECT hint | **DO NOT TOUCH** | release-stable .175; see Principle 2 |
| **JF** | Partial (`JF.cpp`, 12 hits) | template compliance audit (Layers 5–8) | low | working; Phase 6 polish open per codex |
| **Helicase** | Scalar audio (`Helicase.cpp`, 0 hits) | voice-morph NEON (template Layers 4–5) | **medium-high** | "4-shape scalar morph" per template memory — clean target |
| **MultitapDelay (Petrichor)** | **Full 3-pass NEON SoA SVF bank** (spreadsheet 2.6.2.47) | grain path still scalar (Phase 2/3 deferred) | DONE (Phase 1) | **55% → ~30%** at 8 taps + filters (~25pp drop, ~45% relative). Pass C runs 8 SVFs in 2 quads + branchless mode mix + level × pan accumulate. Grain machinery in Pass B stays scalar (gather constraint). Top-end with grains still 60%; Phase 2 (advance prefetch) and Phase 3 (grain NEON) flagged but sub-threshold per recon. Plan: `planning/petrichor-cpu-reduction.md` |
| **Larets** | Scalar (511 lines) | **none — not a multitap.** Stepped multi-effect unit: single-tap delay (`s.buffer[idx]`), single-tap comb, 2-grain Dattorro pitch shifter. No N-tap structure to gather. | **N/A** | the delay-gather memory's claim that the template applies to Larets was wrong; no structural fit. Verified by code read |
| **Filterbank (Tomograph)** | **NEON SoA SVF bank (spreadsheet 2.6.2.42)** | none — DONE | DONE | Internal converted AoS → SoA, 4-band-at-a-time TPT SVF NEON kernel mirroring `mods/mi/rings/dsp/resonator.cc`. Branchless mode dispatch via per-mode bpGain/lpGain bake at block-rate. Padding to multiple-of-4 (no scalar tail). File-level `no-tree-vectorize` killed the auto-vec init surface. `Filterbank.o` lands with **zero** `:64` hints. **Hardware-verified: 8% CPU at stereo + bandCount=16 + max Q, no audible regression.** Plan: `planning/filterbank-neon-refactor.md` |
| **MultibandSaturator (Parfait)** | **Full SoA NEON kernel** (spreadsheet 2.6.2.43-46) | per-band shaper SIMD for same-type case (Phase 6) — situational | DONE (Phase 1-5) | 35% → 25-27% at full-tilt. Per-band AA filter + SVF TPT bank lane-parallel; SVF morph crossfade hoisted to block-rate-baked gains; branchless `useSvfMask` select; block-rate `anySvfActive` fast path; FFT viz at 62 Hz. Plan: `planning/multiband-units-cpu-reduction.md` |
| **MultibandCompressor (Impasto)** | **Full SoA NEON kernel** (spreadsheet 2.6.2.43-46) | none above 2pp threshold | DONE | 27% → 13-15% at full-tilt. Per-band detector/log2/exp2/apply chain runs 3 bands lane-parallel; SC crossover skipped when disabled; makeup × bandLevel pre-mul'd; block-rate `anyCompActive` fast path. FFT rate change reverted on regression (every-4, 94 Hz). Plan: `planning/multiband-units-cpu-reduction.md` |
| **Etcher** | Scalar (550 lines) | unaudited | low | inspect first |
| **Rauschen** | Scalar (531 lines) | likely sequential RNG | low | inspect first |
| **Colmatage** | Scalar (436 lines) | viz-heavy; audio likely simple | low | |
| **GateSeq / TrackerSeq** | Scalar | sequencers — control logic, not DSP | OUT OF SCOPE | |
| **Blanda** | Scalar (152 lines) | control mapping; not a DSP candidate | OUT OF SCOPE | |

### catchall/

| Unit | NEON state | Opportunity | Priority | Notes |
|---|---|---|---|---|
| **AlembicVoice** | Heavy NEON (`AlembicVoice.cpp`, 40 hits) | Layer 7 (struct refs) & Layer 8 (pre-multiply) audit | medium | per template memory; codex Phase 5a-5d-4 done |

### mi/

Recursive sweep (2026-05-14) — the initial top-level grep missed
the eurorack symlinks / override subdirs; corrected here.

| Unit | NEON state | Opportunity | Priority | Notes |
|---|---|---|---|---|
| **Rings** | Modal NEON tightened (mi 1.0.2); FM scalar (no-gather constraint, deferred); String scalar (Phase 3 spike-gated) | Phase 3 String SoA pending hardware spike | partial | Modal: single-accumulator quad + padding-to-multiple-of-4 (mi 1.0.2 / commit pending). FM: see plan `planning/rings-neon-pass.md` Phase 2 — Cortex-A8 NEON has no gather load and `SineFm` is a 4096-entry LUT, so cross-voice NEON would be break-even or negative without polynomial-sine substitution (separate tone-audition gate). String: structural fit but per-string state heavy + delay-line reads inherently scalar; spike-gated. |
| **Clouds** | Partial — `clouds/dsp/grain.h` + `clouds/dsp/pvoc/frame_transformation.cc` | SRC polyphase FIR + ShyFFT butterflies still scalar | medium-high | per todo "Further NEON optimization — ShyFFT butterflies, SRC polyphase FIR." Heavy DSP path; FFT/FIR are their own NEON disciplines (not voice-bus or delay-gather), but well-trodden patterns exist |
| **Plaits** | Scalar (24 engines); 6-op FM extraction queued | per-engine NEON pass (Phase 1-6 in plaits-cpu-reduction.md); separate Hexop FM unit spinoff | **medium-high (planned)** | Engines vary widely — additive/wavetable/FM/modal etc. Recon at `planning/plaits-neon-recon.md`; broad plan at `planning/plaits-cpu-reduction.md`. Hexop FM spinoff at `planning/hexop-fm-unit.md` |
| **Commotio** | Scalar | full NEON pass on backlog | low-medium | per todo "NEON optimization pass" |
| **Warps** | Scalar | low DSP density (small unit) | low | vocoder upsample is a todo but for fidelity, not perf |
| **Stratos** | Scalar | unaudited | low | small unit |
| **Grids / MarblesT / MarblesX** | Scalar | clock / random / sequencer — control logic | OUT OF SCOPE | not DSP-bound |

### biome/

Recursive sweep confirmed zero NEON. Largely small utility units —
parallelism payoff is structurally low (per user). Audited for
completeness.

| Unit | NEON state | Opportunity | Priority | Notes |
|---|---|---|---|---|
| **Canals** | Scalar (233 lines) | NEON vectorize if hardware CPU bench warrants | low | per todo "Bench CPU cost on hardware; NEON vectorize if needed" — Three Sisters fidelity filter |
| **CodescanOsc / CodescanFilter / NR / GestureSeq / DJFilter / LatchFilter / SpectralFollower / VarishapeVoice / FadeMixer / Discont** | Scalar | low DSP density across the board | OUT OF SCOPE | <200 line utility units; not parallelism-shaped |

### peaks / porcelain / scope / kryos

Recursive sweep confirmed zero NEON. Mostly control logic / scope /
sequencer / spectral-freeze utility tools — low DSP density. **Out
of scope** unless a specific CPU complaint surfaces.

## Recommended next moves

In rough order of expected win-per-effort, given current evidence as
of 2026-05-15:

~~**Filterbank (Tomograph)** — DONE at spreadsheet 2.6.2.42.~~

~~**Rings (mi)** — Phase 1 (modal tighten) shipped at mi 1.0.2
(commit a8c8a3c). FM/String closed out structurally — see
`planning/rings-neon-pass.md` and `feedback_neon_no_gather_lut_dsp`.~~

~~**Parfait + Impasto (Multiband Sat / Comp)** — DONE across
spreadsheet 2.6.2.43-46. Impasto 27%→13-15%, Parfait 35%→25-27%.
Per-band SoA NEON + fast paths. Foundation `util/neon_math.h` shipped
in the same arc, now reusable. Plan:
`planning/multiband-units-cpu-reduction.md`.~~

### Active workstreams

1. **Plaits CPU reduction (mi)** — `planning/plaits-cpu-reduction.md`.
   Multi-phase plan covering 21 engines (excludes 6-op FM + Speech).
   Phase 0 foundation library already shipped with multiband. Phase 1
   targets are clean fits with substantial wins:
   - Swarm (~40% engine reduction): 8 oscs → 2 NEON quads
   - Chord (~50%): 4 voices map directly to one quad
   - Additive (~50-60%): 16 partials → 4 per quad (needs polynomial sine)
   - Modal (~50%): clone Rings modal SoA pattern directly

2. **Hexop FM unit (spreadsheet spinoff)** — `planning/hexop-fm-unit.md`.
   Extract Plaits 6-op FM as polyphonic NEON voice on the
   tomf-polygon bus (8 voices × 6 ops SoA, polynomial sine, 2× OS).
   Phase 0 is a hard polynomial-sine FM-tone audition gate. If gate
   passes: probably the single highest-impact optimization remaining,
   since 6-op FM is the heaviest Plaits engine and extracting it gives
   polyphony + oversampling + NEON in one move.

### Queued — not yet planned

3. **Clouds SRC + ShyFFT (mi)** — explicit todos in code. FFT/FIR
   have their own NEON disciplines (well-trodden, not voice-bus or
   delay-gather); heavy DSP path with known headroom. No design doc
   yet — needs recon pass.

4. **Helicase voice morph** — template Layers 4-5 on the scalar
   voice morph. `feedback_neon_voice_bus_template` flags this
   specifically; clean targeted gain.

5. **AlembicVoice** — Layer 7 / Layer 8 audit (struct refs,
   pre-multiply). Small cleanup, low risk. Per-codex (Phase 5
   complete).

6. **JF** — template compliance audit. Working; verify Layers 5/7/8
   before any new feature work.

### Not in scope / rejected
- **Larets** — not a multitap (stepped multi-effect with single-tap
  reads). No structural fit for delay-gather template. Removed from
  candidate list 2026-05-14.
- **Multiband crossover NEON** (block-of-4, 2-lane audio+SC,
  biquad-form) — investigated under multiband arc, all rejected on
  cycle count or FLOP-ratio analysis. The 4-stage cascaded one-pole
  at constant frequency is mathematically near-optimal already.

## Procedural notes (for any pass)

- **SWIG wrapper:** force-clean on any header-with-DSP edit
  (`feedback_swig_header_dep`). The spreadsheet `mod.mk` auto-handles
  this since 2.6.2.29; other packages may not.
- **Hint audit:** after every NEON-touching build, run
  `tools/check-neon-hints.sh testing/am335x/mods/<pkg>/<unit>.o`
  BEFORE installing. Acceptance: zero new `[sp :64]` quad-D spills,
  zero new `[reg :64]` on offsets not derivable from `sp` at 8-byte
  alignment.
- **Whole-`.o` scan, not just `process()`.** Constructors and
  auto-vectorized member-init can emit trap-pattern stores too —
  see `feedback_neon_hint_surfaces` and the Ngoma .165 finding.
- **Hardware verification.** Emulator (x86_64 / aarch64) cannot
  reproduce am335x codegen traps. Every NEON pass must be tested
  on hardware before counting it as landed.

## Cross-refs

- `feedback_neon_voice_bus_template` — voice-bus pattern.
- `feedback_neon_delay_gather` — multitap pattern.
- `feedback_neon_intrinsics_drumvoice` — class-member storage rule.
- `feedback_neon_hint_surfaces` — trap surfaces beyond the obvious.
- `feedback_runtime_branched_dsp_dispatch` — adjacent trap
  (branchless arithmetic masking).
- `feedback_framebuffer_blend_vs_set` — graphics-side gotcha.
- `feedback_viz_encoder_capture_architectural` — architectural-first.
- `feedback_no_paths_of_least_resistance` — pick the option that
  achieves the aim, not the easiest.
- `project_ngoma_codex` — the cautionary tale + bisect history.
- `project_jf_codex`, `project_alembic_codex` — current state of
  those units.
- `planning/visadhara-initial-pass.md` — Visadhara perf arc in detail.
