# NEON optimization opportunities (habitat)

**Audit date: 2026-05-14.** No prior dedicated NEON planning doc
existed — NEON references were scattered across other planning docs
(`network-*`, `jf-initial-pass`, `alembic-phase-8`,
`visadhara-initial-pass`, etc.) and several feedback memories. This
consolidates the picture into a single matrix with prioritization
principles informed by the Visadhara and Ngoma campaigns.

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
| **MultitapDelay (Petrichor)** | No explicit NEON, but **heavily optimized** (`__builtin_prefetch` upfront, fast math, LCG, grain bypass for unity pitch, smoothed delay, etc.) | explicit NEON intrinsics on Pecto-style passes A/C — gap remaining | medium | algorithmic delay-gather tier already landed (`d281acc`, `17074ce`, `3ae8a16`, `8046cd2`). Complication: per-tap **grain machinery** (`kGrainsPerTap` grains per tap with envelope / phase / reverse) doesn't map straight to Pecto's flat tap reads — NEON shape needs a structural choice (per-tap vs per-grain) before any lift |
| **Larets** | Scalar (511 lines) | **none — not a multitap.** Stepped multi-effect unit: single-tap delay (`s.buffer[idx]`), single-tap comb, 2-grain Dattorro pitch shifter. No N-tap structure to gather. | **N/A** | the delay-gather memory's claim that the template applies to Larets was wrong; no structural fit. Verified by code read |
| **Filterbank (Tomograph)** | **NEON SoA SVF bank (spreadsheet 2.6.2.42)** | none — DONE | DONE | Internal converted AoS → SoA, 4-band-at-a-time TPT SVF NEON kernel mirroring `mods/mi/rings/dsp/resonator.cc`. Branchless mode dispatch via per-mode bpGain/lpGain bake at block-rate. Padding to multiple-of-4 (no scalar tail). File-level `no-tree-vectorize` killed the auto-vec init surface. `Filterbank.o` lands with **zero** `:64` hints. **Hardware-verified: 8% CPU at stereo + bandCount=16 + max Q, no audible regression.** Plan: `planning/filterbank-neon-refactor.md` |
| **MultibandSaturator (Parfait)** | Scalar (681 lines); pffft for FFT (NEON via lib) | NEON the per-bin / per-band post-FFT work — RMS (`sqrt(re²+im²)`), biquad envelopes, saturation | medium | FFT itself already NEON via pffft (auto-detects with `-mfpu=neon`); win magnitude depends on what share of CPU lives outside the FFT |
| **MultibandCompressor (Impasto)** | Scalar (494 lines); pffft for FFT (NEON via lib) | same shape as Parfait | medium | per-bin / per-band post-FFT processing |
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
| **Plaits** | Scalar (24 engines) | per-engine audit | low (sweep cost) | engines vary widely — additive/wavetable/FM/modal etc.; not a flat opportunity, each engine is its own shape |
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

In rough order of expected win-per-effort, given current evidence:

~~**Filterbank (Tomograph)** — DONE at spreadsheet 2.6.2.42.~~

~~**Rings (mi)** — Phase 1 (modal tighten) shipped at mi 1.0.2
(commit a8c8a3c). Phases 2 (FM) and 3 (String) closed out without
code work: FM blocked by Cortex-A8 NEON's no-gather constraint on
the `SineFm` LUT (would require polynomial-sine substitution = tone
audition decision, separate work); String's available SIMD axis is
only the filter portion (~25 % mode CPU at the poly=1 sympathetic
best case), modest payoff for invasive refactor. Hardware audit at
poly=4 full-band modal showed ~5 pp improvement from Phase 1 —
modal kernel is structurally near its NEON ceiling. See
`planning/rings-neon-pass.md` and `feedback_neon_no_gather_lut_dsp`
for the constraint-of-record.~~

1. **Clouds SRC + ShyFFT (mi)** — explicit todos. FFT/FIR have
   their own NEON disciplines (well-trodden, not voice-bus or
   delay-gather); heavy DSP path with known headroom.
2. **MultitapDelay (Petrichor) — explicit NEON intrinsics tier.**
   Algorithmic tier already landed (prefetch, fast math, LCG, grain
   bypass). Remaining work is the Pecto-style NEON intrinsics on
   passes A/C; complicated by per-tap grain machinery — needs a
   structural choice (NEON across taps or across grains-within-a-tap)
   before any lift. Not the easy template lift I'd implied.
3. **Parfait + Impasto (Multiband Sat / Comp)** — per-bin and
   per-band processing around the pffft FFT (RMS detection,
   envelope biquads, gain reduction, saturation). FFT itself is
   already NEON'd by the library; win comes from the surrounding
   code. Magnitude less predictable than the bigger wins above.
4. **Helicase voice morph** — template Layers 4–5 on the scalar
   voice morph. Memory flags this specifically; clean targeted gain.
5. **Plaits (mi) — per-engine recon** — 24-engine zoo, no single
   template lift. Per-engine analysis required: additive-style
   engines (Harmonic, Chord, Swarm, Wavetable) likely clean SIMD
   targets; LUT-dominated (FM, Speech) and stateful-sequential
   (Grain, Sample) blocked by the same no-gather constraint that
   shut down Rings FM.
6. **AlembicVoice** — Layer 7 / Layer 8 audit (struct refs,
   pre-multiply). Likely small cleanup, low risk.
7. **JF** — template compliance audit. Working; verify Layers 5/7/8
   before any new feature work.

**Larets removed** from the candidate list. Earlier ordering had it
at #2 based on the `feedback_neon_delay_gather` memory's mention.
Code read disproved that claim — Larets is a stepped multi-effect
unit with single-tap reads, not a multitap. No structural fit for
the delay-gather template. The memory entry should be corrected
(drop Larets; note grain caveat on Petrichor).

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
