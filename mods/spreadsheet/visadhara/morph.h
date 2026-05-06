#pragma once

// Wave-shape morph helper: continuous blend through sin → tri → saw → sq
// per the BIA Morph control. Position 0..1 maps:
//   0.000 = sine
//   0.333 = triangle
//   0.667 = saw
//   1.000 = square
// Linear interpolation between adjacent anchor shapes.
//
// Implementation: analytic per-shape evaluation. Sine via polynomial
// approximation (no libm trig — feedback_package_trig_lut). All shapes
// computed per-sample, crossfaded by morph position.
//
// CPU note: 4 shape evals per voice per sample × 6 voices = 24 evals/sample.
// At 48 kHz that's ~1.15M shape ops/sec. Acceptable for v1; if profile
// shows hotspot, swap for 4×256 LUT gather (matches JF CURVE pattern).

#include <math.h>

namespace stolmine
{
  namespace visadhara_morph
  {
    // Polynomial sine: Bhaskara approximation, accurate to ~1.5% over half
    // cycle. Input phase in [0, 1) (one cycle); output in [-1, +1].
    static inline float poly_sin(float phase)
    {
      // Map phase [0, 1) to t [-1, +1] then sin(πt).
      const float t = 2.0f * phase - 1.0f;
      const float t2 = t * t;
      // Polynomial approximation to sin(π*t)/(π*t) * πt = sin(πt).
      // Bhaskara I: sin(πt) ≈ 16t(1-|t|) / (5 - 4|t|(1-|t|))
      const float at = t < 0.0f ? -t : t;
      const float p = at * (1.0f - at);
      // Rearranged for the [-1,1] symmetry of sine:
      // sin curve is anti-symmetric about t=0
      const float n = 16.0f * p;
      const float d = 5.0f - 4.0f * p;
      const float mag = n / d;
      return t < 0.0f ? -mag : mag;
    }

    // Triangle: peaks at phase=0.25 (+1) and 0.75 (-1), zero crossings at
    // 0.0 and 0.5 — same phase alignment as sine. [-1, +1].
    static inline float tri(float phase)
    {
      // First half [0, 0.5): rises 0 → +1 → 0
      // Second half [0.5, 1): falls 0 → -1 → 0
      if (phase < 0.25f) return 4.0f * phase;
      if (phase < 0.75f) return 2.0f - 4.0f * phase;
      return 4.0f * phase - 4.0f;
    }

    // Saw: rises -1 → +1 across the cycle. To match sine's rising zero-crossing
    // at phase=0, offset so that saw also crosses zero at phase=0.5 (going
    // from -1 to +1 with the discontinuity).
    static inline float saw(float phase)
    {
      // Standard saw: 2*phase - 1 (rises -1 to +1 across [0, 1)).
      // Discontinuity at phase=1→0 wraparound.
      return 2.0f * phase - 1.0f;
    }

    // Square: ±1 stepped at phase 0.5. Phase-aligned with sine zero-crossings.
    static inline float sq(float phase)
    {
      return phase < 0.5f ? 1.0f : -1.0f;
    }

    // Per-shape RMS normalization. With unit peak amplitude:
    //   sine     RMS = 1/√2   ≈ 0.7071
    //   triangle RMS = 1/√3   ≈ 0.5774
    //   saw      RMS = 1/√3   ≈ 0.5774
    //   square   RMS = 1
    // Scale each shape so its RMS matches the lowest (triangle / saw).
    // Peak amplitudes end up ≤ 1 for all shapes. Equal-power crossfade
    // (compute_weights below) handles the cold-spot issue across
    // shape transitions; perceived-loudness disparity from harmonic
    // content (saw / sq sound brighter at equal RMS) is preserved as
    // a deliberate "harmonic build-up" character of the morph sweep.
    static const float kSinScale = 0.8165f;   // √(2/3) — bring sine down to tri RMS
    static const float kTriScale = 1.0f;
    static const float kSawScale = 1.0f;
    static const float kSqScale  = 0.5774f;   // 1/√3 — bring square down to tri RMS

    // Precomputed crossfade weights for one block of samples. Computed
    // once at block rate via compute_weights(); the per-sample sample_w()
    // call is then branchless (no per-sample dispatch).
    //
    // Weights use equal-power (sqrt) crossfade rather than linear to
    // keep RMS roughly constant across the morph sweep. The shapes
    // adjacent in the sweep are not all positively correlated — saw is
    // anti-correlated with both triangle and square — so a linear
    // crossfade produces audible RMS dips ("cold spots") at the
    // midpoints of those transitions. sqrt-equal-power crossfade
    // maintains RMS exactly for uncorrelated pairs and only mildly
    // overshoots for correlated pairs (sin↔tri).
    //
    // Three of the four weights are 0 in any given segment.
    struct Weights
    {
      float w_sin;
      float w_tri;
      float w_saw;
      float w_sq;
    };

    static inline Weights compute_weights(float morph)
    {
      if (morph < 0.0f) morph = 0.0f;
      if (morph > 1.0f) morph = 1.0f;

      Weights w;
      w.w_sin = 0.0f;
      w.w_tri = 0.0f;
      w.w_saw = 0.0f;
      w.w_sq  = 0.0f;

      if (morph < 0.333333f)
      {
        const float a = morph * 3.0f;
        w.w_sin = sqrtf(1.0f - a) * kSinScale;
        w.w_tri = sqrtf(a)        * kTriScale;
      }
      else if (morph < 0.666667f)
      {
        const float a = (morph - 0.333333f) * 3.0f;
        w.w_tri = sqrtf(1.0f - a) * kTriScale;
        w.w_saw = sqrtf(a)        * kSawScale;
      }
      else
      {
        const float a = (morph - 0.666667f) * 3.0f;
        w.w_saw = sqrtf(1.0f - a) * kSawScale;
        w.w_sq  = sqrtf(a)        * kSqScale;
      }
      return w;
    }

    // Per-sample evaluation against precomputed weights. Branchless —
    // computes all four shapes and weights them. CPU cost is roughly 2×
    // the segmented dispatch but the per-sample loop is straight-line
    // safe per feedback_runtime_branched_dsp_dispatch.
    static inline float sample_w(float phase, const Weights &w)
    {
      const float s_sin = poly_sin(phase);
      const float s_tri = tri(phase);
      const float s_saw = saw(phase);
      const float s_sq  = sq(phase);
      return s_sin * w.w_sin
           + s_tri * w.w_tri
           + s_saw * w.w_saw
           + s_sq  * w.w_sq;
    }

    // Legacy linear-crossfade entry point (Phase 1-4). Kept for
    // compatibility while migration to sample_w / compute_weights
    // proceeds. Prefer sample_w going forward.
    static inline float sample(float phase, float morph)
    {
      const Weights w = compute_weights(morph);
      return sample_w(phase, w);
    }
  }
}
