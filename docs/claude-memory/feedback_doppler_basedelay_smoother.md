---
name: Doppler-style baseDelay smoother for multitap delays
description: Pecto's snap-and-fade pattern for zipper-free knob motion. Single pipeline, single read, per-sample-interpolated `baseDelay` scalar via od::LinearRamp. Cortex-A8-safe; matches od::Delay's mFade gating semantics but bypasses the dual-pipeline trap. Reusable for Petrichor / Larets / future multitap delays.
type: feedback
originSessionId: 9c91f43c-1e91-4b56-b89a-49ad6c70cd8a
---
Pecto's `combSize` and V/Oct knob motion produced audible zipper noise
because `mCachedDelaySamples[]` was recomputed per block and the 24-tap
read pointers jumped block-to-block.

**Why:** the SDK's reference smoother (`od::Delay` snap-and-fade with
two read positions per delay line) does not extend cleanly to a 24-tap
multitap on Cortex-A8. Doubling the per-sample NEON pipeline (pass A
+ B + C twice, once per position set) doubled register pressure; gcc
spilled vregs into stack-local NEON ops with `:64` hints that AAPCS
8-byte SP alignment can't satisfy → hardware data-abort
(`.182`, `.183` both crashed; full diagnosis in
`planning/pecto-zipper.md`).

**How to apply:** for any multitap delay with a continuous `baseDelay`
knob (size, pitch, V/Oct), use the Pecto Path B pattern instead of
trying to mirror `od::Delay` verbatim:

1. Class members: `od::LinearRamp mFade`, `float mPrevBaseDelay`,
   `float mCurBaseDelay`. **Don't** add per-tap OLD/NEW position
   caches.
2. Ctor: `mFade.setLength((int)(globalConfig.frameRate * 0.025f))`
   (25 ms ramp, matches `od::Delay`).
3. Process() block setup: compute target `baseDelay`. If
   `mFade.done()`: snap `mPrev <- mCur; mCur <- baseDelay;
   mFade.reset(1, 0)`. Otherwise leave both alone (still ramping).
4. `float *fade = od::AudioThread::getFrame();
   mFade.getInterpolatedFrame(fade);`
5. **Don't** auto-vec block-rate copies of class-member float arrays
   (e.g. tap weights). Wrap in `__attribute__((noinline,
   optimize("no-tree-vectorize")))` helpers — class layout often
   shifts vs the previously-safe baseline and the offset may not be
   8-byte aligned. Pattern: `static void copyFloatArray(float *dst,
   const float *src, int n)`.
6. Per output sample: `float currentBase = fade[i] * mPrev + (1.0f -
   fade[i]) * mCur;`. Pass A's NEON 4-wide inner loop loads
   `s.tapPosition[t]` (static per-block) and scales by
   `vdupq_n_f32(currentBase)` inline — one extra `vmulq_f32` per 4
   taps vs the old cached-delays load. Pass B / Pass C unchanged.
7. End of process: `mFade.step();
   od::AudioThread::releaseFrame(fade);`

**Net effect:** continuous Doppler-style transitions (read pointer
slides smoothly between positions during knob sweeps). Pitch glide
during transitions is the trade — for multitap **combs** it's the
correct musical character (matches analog tape-delay expectation,
and Karplus / sitar / clarinet resonator types are pitch-tracking
by nature anyway). Don't apply this to clean-pitch delays like
straight digital delay where the SDK crossfade is preferred.

**Codegen verification:** Pecto.o NEON `:64` hints went 16 (`.181`)
→ 10 (`.184`); process() specifically 3 → 1. *Fewer than baseline*
because removing `mCachedDelaySamples` eliminated one auto-vec'd
block-rate update target. Single pipeline preserved → no register-
pressure doubling.

**CPU:** ~+5% over baseline (24 muls/sample for tap-delay scaling +
3 ops/sample for currentBase). Pecto stereo went ~6% → ~7-8%.

**When the SDK two-read crossfade IS right:** single-tap delays
where you want amplitude crossfades without pitch shift (od::Delay
itself, Doppler-distinct effects). Don't replicate the dual
pipeline at multitap scale on Cortex-A8 unless register pressure
budget has slack — for Pecto/Petrichor/Larets it doesn't.
