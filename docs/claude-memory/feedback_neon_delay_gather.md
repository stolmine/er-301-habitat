---
name: NEON 3-pass pattern for delay-line multitap gather loops on Cortex-A8
description: Breaking a fused multitap read/interpolate/accumulate loop into compute/gather/combine passes, plus deep prefetch and explicit NEON intrinsics on the arithmetic passes, reduced Pecto stereo CPU from 50% to ~6% at density 24. Applies directly to any buffer-based unit with multiple parallel reads from a shared ring buffer (Petrichor / MultitapDelay, Larets' per-step sample reads, future Lofi delay).
type: feedback
originSessionId: 6486de73-452f-466c-9c43-03152c66fce2
---
## The optimization template

For units with a per-sample inner loop that reads N delay-line taps, interpolates, weights, and accumulates — and where each tap reads a different offset into a large ring buffer — split the single fused loop into three passes per sample:

**Pass A: compute.** For every tap, compute `idx0`, `idx1`, and `frac` into scratch arrays. Pure linear arithmetic plus masked wrap-add. Auto-vectorizes cleanly on gcc `-O3 -mfpu=neon`.

**Pass B: gather.** Scalar `buf[idx0[t]]` / `buf[idx1[t]]` loads. **Cortex-A8 NEON has no gather load** — this cannot vectorize. But it's now isolated, so you can prefetch aggressively: at tap `t` emit `__builtin_prefetch(&buf[idx0[t+K]], 0, 1)` with `K = 4` to `8`. The 192 KB Pecto buffer regularly misses L1, and the deeper K the better (memory latency on am335x is ~100-200 cycles).

**Pass C: combine.** Interp + weight + accumulate. Dense float arithmetic over the scratch arrays. Auto-vectorizes as a reduction.

## Why the pattern wins

1. **Unblocking auto-vec**: the fused loop has a gather *and* a floating-point reduction in the same body — gcc gives up on vectorizing either. Separating them lets the compiler vectorize A and C while leaving the intrinsically-scalar gather alone.

2. **Prefetch window depth**: the fused loop could only prefetch `t+1` effectively (by the time you start tap `t+2`'s loads, the prefetch hint for it has already raced with the demand load). With a dedicated gather pass, prefetching `t+K` for K up to 8 becomes natural and hides far more memory latency.

3. **Cache pressure isolation**: the compute pass touches only `mCachedDelaySamples[]` (a small hot array), the gather pass touches the big ring buffer, the combine pass touches small scratch arrays. Each pass has a roughly single working set.

## Phase 2: explicit NEON intrinsics

After the restructure, wrap passes A and C in explicit NEON intrinsics gated by `#if defined(__ARM_NEON) || defined(__ARM_NEON__)` (macro name varies across toolchains — check both). Scalar fallback for linux build (SSE auto-vec handles it fine) and for `density % 4` tail.

Critical Cortex-A8 gotchas:
- No `vaddvq_f32` — horizontal sum via `vadd_f32(vget_low_f32(v), vget_high_f32(v))` then `vpadd_f32` cascade.
- `vcvtq_s32_f32` truncates toward zero; that equals floor only for non-negative inputs. Wrap values *before* the cast.
- Scratch arrays indexed by NEON stores must be `int32_t*`, not `int*`, on ARM GCC — the compiler is strict about `long int` vs `int` typing even when sizes match. Declare as `int32_t idx0[...]`.
- `vmlaq_f32(acc, a, b)` is the FMA form: `acc + a*b`. Use for interp: `vmlaq_f32(aV, vsubq_f32(bV, aV), fracV)`.
- `vmovl_s16` widens int16→int32, then `vcvtq_f32_s32` + scalar multiply by `1/32767` converts to normalized float.

## Additional block-level cleanups that compound

These aren't NEON-specific but paired with the pass split:

- **Pre-sort tap positions once per change**, not per block. If `cachedDelays[t] = baseDelay * tapPosition[t]` and `baseDelay > 0`, order is preserved under the multiply. Sort `tapPosition[]` (with weights in lockstep) inside the dirty-flagged recompute routine and drop the per-block insertion sort.
- **CSE redundant bufRead calls**. `bufRead(idx0) + (bufRead(idx1) - bufRead(idx0)) * frac` — cache `a = bufRead(idx0)` to a local. GCC doesn't always CSE across the int16→float cast chain.
- **Hoist block-constant branches out of the per-sample loop**. e.g. `if (resonatorType == 3)` every sample → `const bool isSitar = ...` outside, gate per-sample condition on the bool.

## Measured impact (Pecto, stereo, density 24)

| Stage | Stereo CPU | Relative drop |
|---|---|---|
| Baseline fused loop | ~50% | — |
| 3-pass split + 4-ahead prefetch + auto-vec | ~26% | 48% reduction |
| + explicit NEON intrinsics + 8-ahead prefetch | ~6% | another 77% of what remained |

Total ~8× improvement. The split + deep prefetch is the majority of the win; the intrinsics push another ~20 percentage points.

## When to apply

Any unit with:
- Per-sample inner loop reading N ≥ 4 offsets from a shared ring buffer,
- Buffer larger than L1 (~32 KB on am335x),
- Linear interpolation at each read,
- A floating-point accumulation or similar reduction.

**Petrichor / MultitapDelay** is the next obvious candidate — same architecture, tens of taps over a large buffer. **Larets'** per-step sample reads fit if the step count is high. Future **Lofi delay** will almost certainly want this pattern from the start.

## Reference commit

- `00428f3` — Pecto batch A clawback (pre-sort + CSE + hoist + unguarded prefetch — *had a 4% regression* but kept as an isolated revertable commit since later phases didn't need to reverse it).
- `63a4de4` — Phase 1: 3-pass restructure + 4-ahead prefetch.
- `2c2a190` — Phase 2: explicit NEON intrinsics + 8-ahead prefetch.
