---
name: Float-precision-edge wrap on multitap read indices
description: When a NEON-vectorized multitap delay uses `if (p < 0) p += maxDelayF; idx = (int)p` to wrap read positions, the wrap can round exactly to `maxDelayF` when `|p| < ulp(maxDelayF)`, yielding `idx = maxDelay` and an out-of-bounds buffer read. Surfaces under continuous baseDelay smoothing (e.g. Pecto's Doppler slew) where the read pointer slides smoothly across positions and occasionally crosses zero from inside the precision danger zone. Both the NEON path and the scalar tail need the same `idx >= maxDelay -> 0` guard already used for `idx+1`.
type: feedback
originSessionId: 9c91f43c-1e91-4b56-b89a-49ad6c70cd8a
---
**The bug.** Standard multitap wrap math after computing `p = writeIdx -
delay`:

```cpp
if (p < 0.0f) p += maxDelayF;
int idx0 = (int)p;
int idx1 = idx0 + 1;
if (idx1 >= maxDelay) idx1 = 0;   // OK
buf[idx0];                         // possible OOB
```

Float-precision corner: when `p` is barely-negative below
`ulp(maxDelayF)`, the wrap `p + maxDelayF` rounds to exactly
`maxDelayF` (the sub-ulp magnitude of `p` cancels). Then
`(int)maxDelayF = maxDelay`, and `buf[maxDelay]` is one past the
allocated array → hardware data abort on Cortex-A8.

For Pecto's typical 2-second buffer, `maxDelay ≈ 96128`, `ulp ≈
0.0078`. The danger zone is `p ∈ (−0.0078, 0)`. Per-sample, this
is a tiny target — but with 24 taps at 48 kHz that's ~1.15M
tap-evaluations/sec, and over a sustained run with a sliding read
pointer, hits are inevitable.

**Why it surfaced under Doppler smoothing.** The bug is latent in the
wrap math itself. With block-rate cached delays (pre-`.181` Pecto), the
read pointer is constant within a block and only re-evaluated at block
boundaries → very few crossings of the danger zone, no observed
crashes. Add a per-sample baseDelay smoother (`.184+`) and the read
pointer slides continuously across all positions; every transit
through `p = 0` from inside the danger zone hits the bug. Path B
(`.184/.185`, LinearRamp) at 4+ samples/sample slew → ~1–2 s to
crash. Path C (`.186/.187`, one-pole LP) at smaller asymptotic
slew → ~20–30 s to crash. Same root cause, different rates of
hitting it.

**Why x86_64 emu didn't catch it.** Different float arithmetic
behavior + smaller buffer scales + different timing meant the ulp
danger zone was rarely or never crossed under the same scenarios
that crashed Cortex-A8. The bug is real on emu but inert at typical
patch parameters.

**The fix (`.188`).** Symmetric guard on `idx0` matching the existing
`idx1` wrap:

```cpp
// NEON
int32x4_t i0v = vcvtq_s32_f32(p);
uint32x4_t i0WrapMask = vcgeq_s32(i0v, maxDelayVec);
i0v = vbslq_s32(i0WrapMask, zeroIVec, i0v);
// then existing idx1 = idx0+1 wrap
```

```cpp
// scalar
int i0 = (int)p;
if (i0 >= maxDelay) i0 = 0;   // <- new
int i1 = i0 + 1;
if (i1 >= maxDelay) i1 = 0;
```

Cost: 1 NEON cmp + 1 bsl per Pass A iteration, trivial.

**How to apply.** Any multitap delay with the `p = writeIdx - delay;
wrap-once; idx = (int)p` pattern needs this guard, not just on
`idx+1` but on `idx0` itself. Especially required if the
implementation does (or will) per-sample smoothing of read positions.
Block-rate cached delays make hits rare but **not zero** — preemptive
guard is the safe default.

**Audit list (multitaps in habitat that may need this):**
- Pecto — fixed in `.188`.
- AlembicVoice's comb (Pecto-clone). Currently block-rate cached;
  rarely hits but should land a full Doppler-smoother + idx0 guard
  retrofit alongside Pecto's pattern.
- Petrichor (MultitapDelay) — uses similar 3-pass NEON gather per
  `feedback_neon_delay_gather`. Inspect wrap math, add guard if
  pattern matches.
- Larets / Lofi delay — same template per the codex; check.

**Cross-refs:** `feedback_doppler_basedelay_smoother.md` (the smoother
pattern that exposes this bug); `feedback_neon_delay_gather.md` (the
3-pass NEON template that uses this wrap math).
