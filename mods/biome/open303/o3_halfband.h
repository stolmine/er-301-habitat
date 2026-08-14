#pragma once

// Polyphase halfband decimator - habitat port layer, NOT upstream Open303.
// See planning/open303-port.md and LICENSE-Open303.txt.
//
// WHY THIS EXISTS. Upstream decimates with EllipticQuarterBandFilter, a
// 12th-order Direct Form II IIR whose feedback coefficients run to 308.16
// against a b00 of 1.37e-4. A direct-form recursion of that order and
// coefficient spread is ill-conditioned in single precision, so it cannot come
// along when the audio path goes float; and leaving it in double would put a
// 12th-order double-precision IIR on the per-oversampled-sample path, which
// feedback_cortex_a8_no_double_in_hot_loops rules out (no DP NEON on A8).
//
// An FIR halfband has neither problem: no recursion means no conditioning
// issue, float is exact enough by construction, and half the taps are zero.
// One stage decimates 2x -> 1x; two cascaded stages cover the 4x tier.
//
// DESIGN. 63-tap Kaiser(beta = 9.0) windowed sinc, cutoff at half Nyquist,
// normalized to unity DC gain. Taps are compile-time literals computed offline
// (see the plan doc) rather than generated at runtime, deliberately: per
// feedback_package_trig_lut, runtime trig from a package .so has a history of
// miscomputing on am335x, so this file calls no libm at all.
//
// Measured response (normalized to the oversampled rate, 0.5 = its Nyquist):
//   passband ripple to f = 0.21 ... 0.009 dB
//   worst alias-band rejection, f >= 0.292 (everything that could fold into
//   the audible band below 20 kHz at a 48 kHz output) ... -68.5 dB
// Cost is 17 multiply-accumulates per output sample.

#include "o3_config.h"

namespace rosic
{

  // Kaiser(9.0) windowed halfband, unity DC gain, offline-computed literals.
  // File scope with internal linkage rather than static constexpr class
  // members: the tap loop ODR-uses the array, and a constexpr member would
  // then need an out-of-line definition under -std=gnu++11.
  static const int kO3HalfbandPairs = 16;
  static const float kO3HalfbandCenter = 0.500002716f;
  static const float kO3HalfbandOdd[kO3HalfbandPairs] = {
      +0.316995379f, // m = +/-1
      -0.102212842f, // m = +/-3
      +0.057364553f, // m = +/-5
      -0.037032665f, // m = +/-7
      +0.025123026f, // m = +/-9
      -0.017273154f, // m = +/-11
      +0.011805116f, // m = +/-13
      -0.007917642f, // m = +/-15
      +0.005157965f, // m = +/-17
      -0.003231739f, // m = +/-19
      +0.001926095f, // m = +/-21
      -0.001076655f, // m = +/-23
      +0.000553085f, // m = +/-25
      -0.000252507f, // m = +/-27
      +0.000095903f, // m = +/-29
      -0.000025276f, // m = +/-31
  };

  class O3Halfband
  {
  public:
    O3Halfband() { reset(); }

    void reset()
    {
      for (int i = 0; i < kBufLen; i++)
        mBuf[i] = 0.0f;
      mPos = 0;
    }

    /** Consumes two samples at the oversampled rate and returns one sample at
    half that rate. */
    O3_ALWAYS_INLINE float process(float x0, float x1)
    {
      push(x0);
      push(x1);

      // Newest sample sits at mPos-1. The center tap is kHalf back from it;
      // the symmetric pairs sit at kHalf -/+ m for odd m.
      const unsigned n = mPos - 1u;

      float acc = kO3HalfbandCenter * tap(n - kHalf);

      for (int i = 0; i < kO3HalfbandPairs; i++)
      {
        const int m = 2 * i + 1;
        acc += kO3HalfbandOdd[i] * (tap(n - kHalf + m) + tap(n - kHalf - m));
      }

      return acc;
    }

  private:
    // 63 taps -> center at offset 31, 16 nonzero symmetric pairs at odd offsets.
    static const int kHalf = 31;

    // Power-of-two ring so the index wrap is a mask, not a modulo.
    static const int kBufLen = 64;
    static const int kMask = kBufLen - 1;

    O3_ALWAYS_INLINE void push(float x)
    {
      mBuf[mPos & kMask] = x;
      mPos++;
    }

    O3_ALWAYS_INLINE float tap(unsigned absoluteIndex) const
    {
      return mBuf[absoluteIndex & kMask];
    }

    // Class-member storage, never stack-local: per
    // feedback_neon_intrinsics_drumvoice, stack-local float arrays in a hot
    // path are what makes the auto-vectorizer emit trapping aligned loads.
    float mBuf[kBufLen];

    // UNSIGNED deliberately. This advances twice per output sample and never
    // resets, so a signed int overflows after a couple of hours of continuous
    // audio - and signed overflow is undefined behavior that GCC at -O3 is
    // entitled to exploit. Anamnesis was burned by exactly that class of UB
    // (field::hash01, provably exploited by the optimizer). Unsigned wraparound
    // is well defined and the & kMask indexing stays correct across the wrap.
    unsigned mPos;
  };

  /** The decimator chain for the configured tier: one stage at 2x, two at 4x.
  Feeding it is uniform either way - hand it O3_OVERSAMPLING samples, take one
  back. */
  class O3Decimator
  {
  public:
    void reset()
    {
      for (int i = 0; i < O3_HALFBAND_STAGES; i++)
        mStage[i].reset();
    }

#if O3_OVERSAMPLING == 2
    O3_ALWAYS_INLINE float process(const float *x) { return mStage[0].process(x[0], x[1]); }
#else
    O3_ALWAYS_INLINE float process(const float *x)
    {
      // 4x -> 2x, twice, then 2x -> 1x.
      const float a = mStage[0].process(x[0], x[1]);
      const float b = mStage[0].process(x[2], x[3]);
      return mStage[1].process(a, b);
    }
#endif

  private:
    O3Halfband mStage[O3_HALFBAND_STAGES];
  };

} // namespace rosic
