# Pecto Zipper-Noise Smoother — Attack Plan

Status: **planning**, paused 2026-04-29 after `.183` hardware crash.

## Problem

Pecto's combSize and V/Oct knob motion produce audible zipper noise.
Root cause: `mCachedDelaySamples[]` is recomputed once per block from
`baseDelay` (= combSize × sr / 2^voctPitch), so the 24-tap multitap
read positions jump block-to-block when knobs move continuously.

## Attempts so far

| Ver | Approach | Result |
|---|---|---|
| `.181` | (baseline) single-pipeline, no smoothing | zipper noise present |
| `.182` | SDK-faithful dual-pipeline crossfade (od::Delay pattern x24 taps) | crash on Cortex-A8 |
| `.183` | `.182` + noinline+no-tree-vectorize on three block-rate update loops | still crashes |

`.182` had 35 trapping `:64` hints in process(); `.183` has 3 (matching
pre-`.182` baseline). So the auto-vec'd block-rate loops were ONE crash
surface, but not the only one. Remaining surface is the **doubled
per-sample NEON pipeline** itself: pass A + B + C now run twice per
output sample, doubling NEON register pressure across the
gather/interpolate/accumulate chain. Likely producing register-spill
`:64` hints that AAPCS stack alignment doesn't satisfy.

`.183` Pecto.o has 3 process() `:64` hints at 0x10c, 0x968, 0xbd4 —
locations not yet verified against `.181` baseline.

## What we now know about Cortex-A8 traps (lessons consolidated)

- `:64` hint count alone is not a crash predictor (Pecto baseline ships
  with ~16 hints in ctor and works).
- The trap mechanism is `:64` hints **on stack-local NEON ops where
  AAPCS only guarantees 8-byte SP alignment**, OR class-member offsets
  that don't land 8-byte aligned from `this`.
- Auto-vectorized init / copy / scale loops over class-member float
  arrays are a recurring trap surface — fixed via noinline +
  no-tree-vectorize helpers (.165 ctor fix, AlembicVoice's
  initPartialDecayCoeffs / initCombTapDefaults / cacheCombTapDelays,
  `.183`'s copyDelaySamples / recomputeDelaySamples / copyTapWeights).
- **Heavy register pressure in the per-sample loop** can promote
  vreg spills into stack-local NEON stores with `:64` hints (per
  `feedback_neon_hint_surfaces`). This is the surface `.182/.183`
  most likely hit by doubling pass A/B/C.

## Diagnostic phase (cheap, do first)

Goal: pin down whether `.183`'s 3 remaining process() hints are in
the doubled hot loop (would confirm register-spill hypothesis) or
in pre-existing class-member loads (would point elsewhere).

1. `git checkout dc6e19d -- mods/spreadsheet/Pecto.cpp
   mods/spreadsheet/Pecto.h mods/spreadsheet/mod.mk` to a temp branch,
   build `.181` Pecto.o, save aside.
2. Disassemble both `.181` and `.183` Pecto::process(); diff hint
   inventories.
3. For each `.183`-only hint, locate it in source by surrounding
   comment offsets (e.g. "Pass A_old", "Pass C_new").
4. If hot-loop register-spill confirmed → Path A (class-member scratch
   + noinline pass helpers) is the correct minimal fix.
5. If hints look benign → Path A may not help; Path B / C is safer.

## Fix paths

### Path A — preserve dual-pipeline, fix codegen

Faithful to SDK Delay pattern. Higher CPU but cleanest crossfade
character (no pitch shift during transitions).

Changes from `.183`:
- All 10 per-tap scratch arrays become **class members** (heap-
  allocated via Pecto instance, no AAPCS dependency):
  - existing: `mIdx0[64]`, `mIdx1[64]`, `mFrac[64]`, `mSA[64]`, `mSB[64]`
  - new: `mIdx0_0[64]`, `mIdx1_0[64]`, `mFrac_0[64]`, `mSA_0[64]`, `mSB_0[64]`
- Each NEON pass becomes a `__attribute__((noinline,
  optimize("no-tree-vectorize")))` member function:
  - `passA(const float *cachedDelays, int density, int writeIdx,
     int maxDelay, int32_t *idx0Out, int32_t *idx1Out,
     float *fracOut)`
  - `passB(int density, const int32_t *idx0, const int32_t *idx1,
     int16_t *sAOut, int16_t *sBOut)` (with prefetch)
  - `passC(int density, const int16_t *sA, const int16_t *sB,
     const float *frac, const float *tapWeight,
     float *wetOut, float *lastTapOutOut)`
- Per output sample: call passA(old) + passA(new), passB(old) +
  passB(new), passC(old) + passC(new), then crossfade.
- noinline gives gcc a clean register window per call so it can't
  spill across passes.

Risk: still might trap if class-member offsets aren't 8-byte aligned
for `vld1.64 :64` reads on the per-tap arrays. Verify offsets via
`offsetof` in a sanity-check compile assertion.

CPU: ~2x .181 multitap cost + helper-call overhead (~5%). Probably
total 11-13% stereo. Tight on Cortex-A8.

### Path B — single read + per-sample-smoothed baseDelay

Departs from SDK pattern. Continuous Doppler-style transitions during
knob sweeps (read position moves smoothly = pitch glide). Lower CPU.
Likely **more musical for a multitap comb filter** — matches analog
tape-delay character users expect from sweeping a delay knob.

Mechanism:
- Keep `LinearRamp mFade` member.
- Per process(): if `mFade.done()`, snapshot
  `mPrevBaseDelay <- mCurBaseDelay`, set
  `mCurBaseDelay <- baseDelay`, `mFade.reset(1, 0)`.
- `mFade.getInterpolatedFrame(fade)` → 128 per-sample weights
  (1.0 → 0.0).
- Per output sample (inside existing single inner loop):
  - `currentBase = fade[i] * mPrevBaseDelay + (1 - fade[i]) * mCurBaseDelay`
  - Compute tap delays inline: `tapDelay = currentBase * tapPosition[t]`
  - Compute idx0/idx1/frac per-tap from tapDelay
  - rest of pipeline unchanged
- mFade.step() at end of process.

Single pipeline → register pressure unchanged from `.181` → no
register-spill trap surface introduced.

Cost: 1 mul + 1 sub + 1 add per sample for currentBase + 24 muls per
sample for tap delays + per-tap idx/frac compute (was already there,
just moved inside the per-sample loop instead of pre-cached). Net:
+24 muls/sample + small bookkeeping ≈ ~5% CPU over `.181`.

Risk: very low. No new NEON state. No pipeline doubling. The
recompute of `tapDelay` per sample replaces the existing block-rate
recompute of `mCachedDelaySamples`. Need noinline+no-tree-vectorize
on any per-block helper that touches contiguous class-member arrays
(none introduced if we compute tapDelay as a scalar inside the
per-sample loop).

### Path C — single read + one-pole LP smoother on baseDelay

Cheapest variant. Asymptotic settling rather than linear ramp; 25ms
time constant via tuned alpha.

Mechanism:
- Per output sample:
  `s.smoothedBase += (baseDelay - s.smoothedBase) * alpha;`
  (alpha = 1 - exp(-1 / (0.025 * 48000)) ≈ 0.000833)
- rest as Path B with `s.smoothedBase` instead of `currentBase`.

Quality: smoother than block-rate, but not bit-perfect linear ramp.
For long sweeps the LP lag is audible (~25ms phase delay). For knob
flicks it's indistinguishable from Path B. Lightest NEON load — no
LinearRamp, no fade buffer.

CPU: smallest change. ~1% over `.181`.

## Recommendation

**Path B first.** Reasons:
1. Lowest trap surface — no dual pipeline = no register-pressure
   doubling. The hypothesized `.182/.183` crash mechanism is bypassed
   entirely.
2. **Better musical fit for a multitap comb filter.** SDK Delay's
   crossfade pattern is for clean-pitch delays where you want
   amplitude transitions without Doppler. Pecto is a comb filter
   tuned to pitched feedback (Karplus-Strong / sitar / clarinet
   resonator types) — it's MEANT to track pitch as you sweep.
   Doppler glide matches the analog comb-filter character.
3. CPU headroom — ~5% vs ~12% for Path A.
4. If Path B is musically wrong, Path A is still available as
   fallback (or Path C as cheaper variant).

## Implementation order for Path B

1. Diagnose `.183` hot-loop hints first (cheap, informs whether
   Path A would even work).
2. Revert `.182/.183` from main: `git revert c752541 7662192`
   (creates two revert commits) OR start fresh on `.181`.
3. Add `LinearRamp mFade` + `mPrevBaseDelay` + `mCurBaseDelay` to
   Pecto.h.
4. ctor: `mFade.setLength((int)(globalConfig.frameRate * 0.025f))`.
5. process() block setup:
   - compute new `baseDelay`
   - if `mFade.done()`: `mPrevBaseDelay = mCurBaseDelay;
     mCurBaseDelay = baseDelay; mFade.reset(1, 0);`
   - `float *fade = AudioThread::getFrame();
     mFade.getInterpolatedFrame(fade);`
6. process() inner loop:
   - `float currentBase = fade[i] * mPrevBaseDelay + (1.0f - fade[i]) * mCurBaseDelay;`
   - Pass A inline: per tap, `tapDelay = currentBase * s.tapPosition[t]`,
     compute p / idx0 / idx1 / frac directly. NEON 4-wide using
     `vdupq_n_f32(currentBase)`.
   - Pass B / Pass C as in `.181` (single set of scratch arrays,
     stack-local since register pressure unchanged from baseline).
7. process() end:
   - `mFade.step(); AudioThread::releaseFrame(fade);`
8. Remove `mCachedDelaySamples0` and `mCachedDelaySamples` from header
   if no longer needed (Path B computes tap delays inline per sample,
   no caching).
   - Or keep `mCachedDelaySamples` as scratch for the recompute, no
     ZIPPER-causing block-rate behavior because it's recomputed per
     sample anyway.
9. Tier 2 verify: Pecto.o NEON :64 hints should be ~16 (matching `.181`
   baseline). No new auto-vec'd patterns.
10. Hardware listen-test:
    - Sweep combSize, listen for zipper. Should be replaced by smooth
      pitch glide.
    - V/Oct CV continuous, listen for clean tracking.
    - Knob ratchets (density, pattern, slope) still snap (acceptable —
      categorical changes).
11. CPU measurement on LoadView per-chain readout. Target: ≤ 8% stereo
    (vs `.181`'s ~6%).

## Open questions

- Does the LinearRamp's per-sample fade weight need to apply to JUST
  baseDelay, or also to tapWeight when slope changes? Probably only
  baseDelay — slope/density/pattern changes are categorical and rare.
- Should we also smooth feedback / mix? Probably not — those don't
  read out of a delay line, so block-rate jumps don't audibly zipper
  (just a clean amplitude step).
- Is per-sample `currentBase * tapPosition[t]` cheap enough? 24 muls
  per output sample × 128 samples = 3072 muls per block. At 48kHz
  block rate (~375 Hz), that's ~1.15 MFLOPS. Trivial.

## Files to touch

- `mods/spreadsheet/Pecto.h` — add `mFade`, `mPrevBaseDelay`,
  `mCurBaseDelay`. Remove (or repurpose) `mCachedDelaySamples0`.
- `mods/spreadsheet/Pecto.cpp` — restructure process() per above.
- `mods/spreadsheet/mod.mk` — bump to `.184`.

## If Path B fails (musical or technical)

1. **Sounds wrong** (e.g., user prefers crossfade character over
   Doppler glide): try **Path A** with class-member scratch +
   noinline pass helpers. Higher CPU but the musical character users
   liked from SDK Delay.
2. **Still crashes**: the trap is somewhere else entirely. Run gdb
   on the linux emu build under DRUMVOICE_TRACE-style instrumentation
   to verify the snap-and-fade logic at least RUNS correctly in emu.
   Compare instrumented emu trace against hardware behavior.
3. **CPU too high**: drop to **Path C** (one-pole LP), saves the
   LinearRamp + getInterpolatedFrame overhead.
