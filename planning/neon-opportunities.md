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
| **MultitapDelay** | Scalar (877 lines) | delay-gather template | **high** | likely underpins Petrichor/Lofi presets (Lua refs) |
| **Larets** | Scalar (511 lines) | delay-gather template | **high** | delay-shaped; named in delay-gather memory |
| **MultibandSaturator** | Scalar (681 lines) | per-band parallelism — atypical | low | FFT-y; different pattern |
| **MultibandCompressor** | Scalar (494 lines) | similar to MBS | low | |
| **Filterbank** | Scalar (632 lines) | sequential filter state — may not vectorize | low | inspect for parallel-band opportunity |
| **Etcher** | Scalar (550 lines) | unaudited | low | inspect first |
| **Rauschen** | Scalar (531 lines) | likely sequential RNG | low | inspect first |
| **Colmatage** | Scalar (436 lines) | viz-heavy; audio likely simple | low | |
| **GateSeq / TrackerSeq** | Scalar | sequencers — control logic, not DSP | OUT OF SCOPE | |
| **Blanda** | Scalar (152 lines) | control mapping; not a DSP candidate | OUT OF SCOPE | |

### catchall/

| Unit | NEON state | Opportunity | Priority | Notes |
|---|---|---|---|---|
| **AlembicVoice** | Heavy NEON (`AlembicVoice.cpp`, 40 hits) | Layer 7 (struct refs) & Layer 8 (pre-multiply) audit | medium | per template memory; codex Phase 5a-5d-4 done |

### biome / mi / peaks / porcelain / scope / kryos

No NEON anywhere (verified). OOB ports, scope/utility tools — low
DSP density. **Out of scope** unless a specific CPU complaint
surfaces.

## Recommended next moves

In rough order of expected win-per-effort, given current evidence:

1. **MultitapDelay** — apply delay-gather. High confidence (template
   proven on Pecto), likely underpins Petrichor / Lofi presets too.
   Single biggest available win.
2. **Larets** — apply delay-gather. Same template, smaller file but
   same shape.
3. **Helicase voice morph** — template Layers 4–5 on the scalar
   voice morph. Memory flags this specifically; clean targeted gain.
4. **AlembicVoice** — Layer 7 / Layer 8 audit (struct refs,
   pre-multiply). Likely small cleanup, low risk.
5. **JF** — template compliance audit. Working; verify Layers 5/7/8
   before any new feature work.

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
