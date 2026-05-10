# Network unit — optimization plan

**Context.** Network at catchall 0.3.53 hits ~75–80% CPU at glitch=1
+ density=1 (down from ~91% with the 0.3.53 active-stutter cap).
Sound engine is feature-complete; remaining work is performance.
Target: < 60% at full settings to leave headroom for chain
neighbors.

This plan covers every NEON / non-libm optimization opportunity in
the unit. Ordered cheapest→biggest gain so we ship incremental
checkpoints and bail early if measurements show the high-effort
items aren't worth the complexity.

## Per-loop audit (current state, 0.3.53)

### Per-sample (hot path, runs FRAMELENGTH × call)

| Loop | Lines | Status | Cost rank |
|---|---|---|---|
| Smoother step (gainL/R, fbWeight) | ~1308 | NEON 4-wide | low |
| Pass A (dual-read advance + bidirectional wrap) | ~1345 | NEON 4-wide | low |
| Pass B (scalar gather sA/sB) | ~1401 | scalar (no A8 gather) | medium |
| Pass C (crossfade + L3 LP + G8 crush + accumulate) | ~1416 | NEON 4-wide | medium |
| **Scalar stutter pass** | ~1547 | **scalar** | **HIGH** |
| Feedback recycle (tanh + DC + allpass + buf write) | ~1700 | scalar (single stream) | low |

### Block-rate (runs once per call, but iterates activeTaps)

| Loop | Lines | Status | Cost rank |
|---|---|---|---|
| Per-tap LFO loop (S1/S2/L3 coeff) | ~480 | scalar | medium |
| Mode mutex assignment | ~672 | scalar | low |
| G3 mute mode-gating | ~700 | scalar | tiny |
| G8 crush setup (per-CRUSH-tap, w/ powf) | ~720 | scalar + libm | medium |
| Feedback selection | ~830 | scalar | low |
| Block-rate dual-read shift | ~860 | scalar | low |
| G5 scrub offset | ~890 | scalar | tiny |
| G6 reverse alignment | ~915 | scalar | tiny |
| G4 transient detector + dispatch | ~960 | scalar | low |
| G2 stutter trigger / state | ~1020 | scalar | low |
| G7 respawn lifetime | ~415 | scalar | tiny |
| ZC alignment (sign-change scan) | ~1180 | scalar | low |

### libm calls in hot or block-rate paths

| Call | Where | Cost | Mitigation |
|---|---|---|---|
| `powf(2, 12-p*9.5)` | G8 crush setup, per CRUSH tap per block | ~80 cycles/call | LUT (Opt 2) |
| `expf(-baseCutoffHz * 2π/sr)` | L3 LP coeff, once per block | ~60 cycles/call | minor — single call |
| `logf(kCutoffRatio)` | L3 LP coeff, once per block | ~50 cycles/call | minor — single call (and constant!) |
| `sqrtf` | G7 respawn unit-disk clamp + G5 scrub | per respawn | minor — rare |
| `floorf`, `fabsf` | many places | ~3 cycles/call | already inlined |

---

## Optimizations (ranked by gain × low risk)

### Opt 1 — NEON-vectorize per-sample stutter pass *(highest gain)*

**Loop**: stutter scalar playback at lines ~1547, runs over `activeStutterCount`
(0..16) per sample × FRAMELENGTH. Currently scalar:
- Load `mTapStutterReadPtr[t]`, conditional wrap, compute iptr/iptr2, frac.
- 2 random buffer reads (scalar — gather stays scalar on A8).
- Linear interp.
- 3 accumulate FMAs into wetL/wetR/fbSum.
- Advance ptr + posInLoop, conditional loop-wrap reanchor.

**NEON approach**:
- Process 4 stutter taps per inner iteration (when activeStutterCount ≥ 4).
- Load 4 ptrs as `float32x4_t`; vectorize wrap, iptr conversion, frac.
- Per-lane scalar gather (extract iptr/iptr2 from vector, load buf[]); pack 4 sample-pair results back into `float32x4_t a` and `float32x4_t b`.
- NEON FMA for linear interp `sample = a + (b - a) * frac`.
- 3 NEON FMAs into accumulator vectors (per-tap-gain × sample → wet[L/R/fb]Vec).
- After loop, horizontal-sum the accumulator vectors.
- Advance: 4 ptrs += speed in NEON, posInLoop += speed in NEON.
- Loop wrap: branchy, falls back to per-lane scalar if any lane wraps. Or
  branchless mask-blend: `posInLoop = (posInLoop >= loopSamples) ? (posInLoop - loopSamples) : posInLoop`. Iteration decrement still scalar.
- Tail loop for activeStutterCount % 4 remainder.

**Per-tap gain accumulators**: instead of scalar `wetL +=`, build per-iteration `float32x4_t wetLAcc` etc. and horizontal-sum once at end of stutter pass. Already the pattern in Pass C.

**Expected gain**: ~30–40% reduction on stutter pass cost. At full settings (~16 stutter taps × 256 samples × ~20 ops scalar → ~80K ops/block), NEON 4-wide cuts the math to ~20K ops/block plus gather. Net CPU savings: ~8–12 percentage points.

**Risk**: medium. Per-lane gather requires careful int extract from int32x4_t. Loop wrap on per-lane basis needs branchless mask. NEON state arrays (posInLoop, readPtr, speed, etc.) need 16-byte alignment guarantees.

**State changes**: none. Existing state arrays already 64-element `float[]`, naturally aligned to cache lines.

**Verification**: NEON :64/:128 hint check, audible regression test (glitch=1 should sound identical).

### Opt 2 — `powf` → LUT for G8 bitcrush level *(easy win)*

**Loop**: G8 crush setup (line ~755), `powf(2.0f, 12.0f - bitParam * 9.5f)`
called per CRUSH-mode tap per block (~12 taps max, ~187 blocks/sec → ~2200 calls/sec).

**LUT approach**:
- Precompute static `kBitcrushLvlLUT[64]` at compile time: `lvl[i] = pow(2, 12 - i/63 × 9.5)` for i in [0, 63].
- Lookup: `int lutIdx = (int)(bitParam * 63.5f)` (clamped to [0, 63]); `bitLvl = kBitcrushLvlLUT[lutIdx]`.
- Inverse: precompute `kBitcrushInvLvlLUT[64]` to skip the divide too.

**Expected gain**: ~80 cycles/call → ~3 cycles/call = ~1.5% CPU reduction at glitch=1, density=1. Modest but free.

**Risk**: low. LUT quantizes bitParam to 64 levels; perceptually indistinguishable. Both lvl and invLvl LUTs can be `static const float[64]` initialized at compile time via `constexpr` calls or hand-precomputed table.

**State changes**: 2 × 256-byte static const arrays. Trivial.

**Verification**: spot-check a few LUT entries match `powf` to 6+ decimals. Audible test: bitcrush character at extreme settings unchanged.

### Opt 3 — NEON-vectorize block-rate per-tap LFO loop *(medium gain, more code)*

**Loop**: lines ~480–630, runs activeTaps (up to 64) per block. Computes:
- S2 LFO rate hash → mTapLfoRate[i]
- S1 detune hash → mTapDelayTarget[i] += pitchDetune
- modOffset = poly_sin(phase) × kLfoDepthSamples
- G1 S&H clock advance + snapshot
- L3 LP coefficient hash + compute

**NEON approach**:
- Per-iteration of 4 taps: load 4 mLfoPhase, advance, wrap, store. Trivial NEON.
- Hash compute (LCG step with golden-ratio multiplier) is NEON-friendly: vmulq_u32 + vaddq_u32. 4 hashes in parallel.
- `poly_sin(phase)` is the bottleneck — LUT lookup or polynomial. Currently scalar; check if poly_sin is small enough to inline 4-wide. If polynomial-based, it vectorizes; if LUT-based, gather stays scalar.
- L3 LP coeff: hash-based variation × baseCoeff. Vectorizable.
- G1 S&H: per-tap conditional snapshot. Could mask-blend.

**Expected gain**: ~3–5% block-rate CPU. Block-rate is a smaller fraction of total than per-sample, so even good NEON gains here are modest.

**Risk**: medium-high. The LFO loop has many independent computations (S1/S2/L3/G1), each per-tap. Re-architecting to be NEON-friendly while maintaining current behavior is non-trivial. Benefit uncertain until measured.

**State changes**: ensure all per-tap state arrays are vectorizable layout (already are).

**Verification**: NEON :64/:128 hint check, A/B with 0.3.53 — bit-exact match expected for deterministic hashes.

### Opt 4 — Tune Pass B prefetch distance *(near-zero effort)*

**Loop**: Pass B at line ~1401. Currently `__builtin_prefetch(&buf[mTapOldReadIdx[t+8]])`. 8-ahead chosen empirically.

**Tuning**: try 4-ahead, 12-ahead, 16-ahead. Cortex-A8 prefetch latency ~20 cycles to L2; with ~3 cycles per tap iteration, 8-ahead is theoretically right. But empirical varies by buffer-resident-in-L2 state.

**Expected gain**: 0–2% CPU at full settings. Cheap experiment.

**Risk**: minimal.

### Opt 5 — Cache `expf`/`logf` results in L3 LP coeff *(tiny)*

**Loop**: line ~470 area, computes `expf(-decay * logf(kCutoffRatio))` and `1 - expf(-baseCutoffHz × 2π/sr)` once per block.

**Mitigation**:
- `logf(kCutoffRatio)` is constant → replace with `static const float kLogCutoffRatio = logf(6.0f)` once at startup, or just `1.7917595f` literal.
- `expf` for baseCutoffHz: 64-entry LUT indexed by decay (block-rate, single call, but cumulative over many blocks).

**Expected gain**: ~0.5% CPU, but trivially easy.

**Risk**: zero.

### Opt 6 — Pass B + Pass C fusion *(speculative)*

**Idea**: instead of staging through int16 sA/sB scratch arrays, gather + s16→f32 + interp directly in one fused per-sample 4-wide loop. Would eliminate the scratch arrays' 2 × 64-byte stack usage and one round-trip through L1.

**Risk**: high. Would require restructuring the gather, which might lose prefetch benefits or cause register pressure issues. Defer until other opts measured.

### Opt 7 — Branchless decimate counter in G8 *(already done)*

The G8 decimate counter is already branchless NEON in Pass C (vbsl-based wrap). No further work.

---

## Implementation order

Each step is independently shippable with measurable CPU validation
on hardware.

1. **Opt 4 + Opt 5** (cheap wins, low risk) — single combined commit, ~0.5–2% gain. Tunes prefetch distance and replaces libm constants. Quick sanity check before harder work.

2. **Opt 2 — `powf` → LUT** — single commit, ~1.5% gain. Audible regression test only.

3. **Opt 1 — NEON stutter pass** — single commit, ~8–12% gain. Largest single win. Most testing.

4. **(Stop point — measure CPU.)** If we're under target, ship. Otherwise:

5. **Opt 3 — NEON LFO loop** — single commit if time allows. Diminishing returns.

After each step:
- `make ARCH=am335x` clean
- NEON :64/:128 hint check (must stay 0)
- vtable check (must stay V/COMDAT)
- Hardware audition: glitch=0 bit-exact regression, glitch=1 audible match
- CPU meter readings at various density/glitch sweep points

## Critical files

**To modify (in order):**
- `mods/catchall/Network.h` — all opts touch this file.
- `mods/catchall/mod.mk` — PKGVERSION bump per step.

**To reference (no edit):**
- `er-301/mods/core/objects/granular/Grain.cpp` — NEON-vectorized SIMD grain rendering (`hal/simd.h` `simd_*` helpers). Reference for clean per-sample NEON-with-gather patterns.
- `mods/spreadsheet/Pecto.cpp` — NEON 3-pass pattern for multi-tap delay; already in our DNA but worth reviewing.

## Verification matrix (per-step)

| Step | Build linux | Build am335x | NEON hints=0 | vtable V | Lint clean | Hardware audition | CPU meter |
|---|---|---|---|---|---|---|---|
| Opts 4 + 5 | ✓ | ✓ | ✓ | ✓ | ✓ | regression test | ≤ baseline |
| Opt 2 | ✓ | ✓ | ✓ | ✓ | ✓ | crush character match | ≤ baseline |
| Opt 1 | ✓ | ✓ | ✓ | ✓ | ✓ | full glitch character match | ≤ baseline − 8% |
| Opt 3 | ✓ | ✓ | ✓ | ✓ | ✓ | full match | ≤ prior − 3% |

## Memory references

- Header-only inline preserved.
- NEON 4-wide patterns retained, no `:64`/`:128` hints.
- All NEON-touched state class members.
- Force-clean SWIG wrapper between header edits.
- No runtime-branched DSP dispatch (per `feedback_runtime_branched_dsp_dispatch`).
- LUTs at compile-time `static const`, not runtime-init (per
  `feedback_package_trig_lut`).
- PKGVERSION bump per build, install to `~/.od/rear/`.
