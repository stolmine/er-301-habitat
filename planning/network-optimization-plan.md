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

**Reference pattern**: `er-301/mods/core/objects/granular/MonoGrain.cpp:90-128` — canonical scalar-gather-4 + SIMD-interp loop. Per output sample: scalar-gather 4 (phase + 3 buffer values) into stack arrays, then one call to `simd_quadratic_interpolate_with_return(...)`. Same template applied to our stutter playback (linear interp, not quadratic) with `simd_linear_interpolate`.

**Loop**: stutter scalar playback at lines ~1547. Currently per stutter tap per sample: load ptr, wrap, compute iptr/iptr2/frac, 2 buf reads, linear interp, 3 FMA accumulate, advance + loop-wrap reanchor.

**NEON approach** (per-sample, 4 stutter taps in parallel):
- For each batch of 4 stutter taps within the per-sample loop:
  - Load 4 `mTapStutterReadPtr` as `float32x4_t`. Wrap branchlessly (vbsl + vsubq).
  - Convert to int via `vcvtq_s32_f32`; compute frac = ptr − cvtq_f32_s32(int).
  - **Scalar gather**: extract 4 lanes via `vgetq_lane_s32`, load `recent0[i] = buf[iptr] * scale`, `recent1[i] = buf[iptr+1] * scale`. (Gather stays scalar — Cortex-A8 has no NEON gather. Same compromise as Pass B.)
  - Call `simd_linear_interpolate(out, recent0, recent1, frac_array)` to get 4 interpolated sample values.
  - Vector multiply-accumulate into 3 accumulator vectors: `wetLVec`, `wetRVec`, `fbVec` using gathered `mTapStutterGainL/R/FbW`.
  - Advance: 4 ptrs += speed (NEON), posInLoop += speed (NEON), branchless loop-wrap (compare + bsl).
- Horizontal-sum accumulators at end of stutter pass (vpadd pattern from Pass C).
- Tail loop for activeStutterCount % 4 remainder.

**Iteration counter**: still scalar (per-tap state, not per-sample state machine). Decrement happens inside the loop-wrap branch — extract per-lane wrap mask, scalar decrement only on lanes that wrapped. Cheap.

**Expected gain**: ~30–40% reduction on stutter pass cost. ~16 stutter taps × 256 samples × ~20 ops scalar → ~80K ops/block; NEON 4-wide cuts math+ptr-management to ~20K + gather (gather ~20K reads stays). Net CPU savings: ~8–12 percentage points.

**Risk**: medium. Per-lane gather via `vgetq_lane_s32` requires careful index extraction. Loop wrap branchless mask must handle per-lane independently. State arrays already 64-element float — naturally aligned to cache lines but check NEON load alignment hints in objdump output.

**Reuses from `hal/simd.h`**: `simd_linear_interpolate`. Possibly `simd_invert` if we precompute `1/loopSamples` (currently we compare directly).

**Verification**: NEON :64/:128 hint check (must stay 0), audible bit-match at glitch=1, CPU meter shows ~8% drop at full settings.

### Opt 2 — `powf` → `simd_pow` 4-wide for G8 bitcrush level *(easy win)*

ER-301 publishes `hal/simd.h` (extern "C") with `simd_pow(float32x4_t x, float32x4_t m)` — NEON 4-wide transcendental. Cleaner than a LUT, full precision, and processes 4 CRUSH-mode taps in one call.

**Loop**: G8 crush setup (line ~755), `powf(2.0f, 12.0f - bitParam * 9.5f)` called per CRUSH-mode tap per block.

**Approach**:
- Build a list of CRUSH-mode tap indices at block-rate (similar to active-stutter list pattern).
- Process in batches of 4: load 4 `bitParam` values, compute `12.0f - bitParam × 9.5f` in NEON, call `simd_pow(2.0f, result)`, store 4 `bitLvl` values. Use `simd_invert` for `1/bitLvl`.
- Tail loop for activeCrush % 4 remainder.

**Expected gain**: ~80 cycles/call scalar → ~25 cycles per 4-tap NEON call ≈ ~85% reduction in time spent in this path. Net CPU saving small (G8 is block-rate, ~12 taps/block) but easy.

**Risk**: low. `simd_pow` is bit-exact NEON; values match scalar `powf` to ULP. Need to add `#include <hal/simd.h>` and ensure linker pulls in the implementation (it's in `er-301/hal/...`).

**State changes**: none. Just compute the same values via the simd helper.

**Verification**: spot-check a few values match scalar `powf`. Audible: bitcrush character unchanged.

### Opt 3 — NEON-vectorize block-rate per-tap LFO loop *(medium gain, more code)*

**Loop**: lines ~480–630, runs activeTaps (up to 64) per block. Computes:
- S2 LFO rate hash → mTapLfoRate[i]
- S1 detune hash → mTapDelayTarget[i] += pitchDetune
- modOffset = poly_sin(phase) × kLfoDepthSamples
- G1 S&H clock advance + snapshot
- L3 LP coefficient hash + compute

**NEON approach (now viable thanks to `hal/simd.h`)**:
- Hashes (LCG step + golden-ratio multiplier) → NEON 4-wide via vmulq_u32 + vaddq_u32.
- `poly_sin(phase)` was the bottleneck — replaced by `simd_sin(phase × 2π)` from `hal/simd.h`. 4 sins per call.
- LFO phase advance, modOffset compute, S1 detune, L3 LP coeff: all NEON-friendly arithmetic.
- G1 S&H is per-tap conditional — branchless mask-blend (compare clock vs threshold, bsl-blend snapshot value).

**Expected gain**: ~3–5% block-rate CPU. Block-rate is smaller fraction of total than per-sample, so gains modest. But code becomes cleaner once helpers do the heavy lifting.

**Risk**: medium. Re-architecting per-tap independent computations into 4-wide is non-trivial; needs careful per-tap state alignment. Some bit-divergence between `simd_sin` and our existing `network_trig::poly_sin` is possible (different polynomial approximation). Need spot-check audible match.

**State changes**: ensure per-tap state arrays are 16-byte aligned (already are by virtue of `kMaxNetworkTaps = 64` × 4 bytes = cache-line aligned).

**Verification**: NEON :64/:128 hint check, A/B audible match. If `simd_sin` produces audibly different LFO character vs `poly_sin`, may need to keep the existing trig helper for lush primitives and only NEONize the parts that don't depend on it.

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

## Reusable infrastructure: ER-301 `hal/simd.h`

Public header `er-301/hal/simd.h` (extern "C") provides NEON 4-wide
helpers we should use throughout:

- **Transcendentals**: `simd_log`, `simd_exp`, `simd_pow`,
  `simd_sin`, `simd_cos`, `simd_sincos`, `simd_invert`.
- **Interpolation**: `simd_linear_interpolate`,
  `simd_quadratic_interpolate_with_return`,
  `simd_interpolate_env_accumulate`.
- **Envelopes**: `simd_sine_env`, `simd_hanning`.
- **Comparisons**: `simd_any_greater`, `simd_first_positive`.

Implementation lives in the firmware itself
(`er-301/hal/...`); no symbol-resolution issues for packages.

Reference uses:
- `er-301/mods/core/objects/granular/MonoGrain.cpp:90-128` —
  scalar-gather-4 + SIMD-interp template, identical structure to
  what we need for the stutter pass.
- `er-301/mods/core/objects/granular/Grain.cpp:142,161` —
  `simd_sine_env` and `simd_hanning` window generation.

Adopt these instead of writing our own NEON for trig/interp paths.
The remaining bespoke NEON in Network — Pass A advance with
bidirectional wrap, Pass C crossfade-with-LP-and-crush — has no
direct simd.h equivalent and stays hand-written.

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
