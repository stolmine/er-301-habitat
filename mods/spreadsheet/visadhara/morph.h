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

    // Morph-blend the 4 anchor shapes by position 0..1.
    static inline float sample(float phase, float morph)
    {
      // Clamp morph to [0, 1].
      if (morph < 0.0f) morph = 0.0f;
      if (morph > 1.0f) morph = 1.0f;

      const float s_sin = poly_sin(phase);
      const float s_tri = tri(phase);
      const float s_saw = saw(phase);
      const float s_sq  = sq(phase);

      // Three crossfade segments. morph=0 → s_sin, morph=1 → s_sq.
      if (morph < 0.333333f)
      {
        const float a = morph * 3.0f;
        return s_sin * (1.0f - a) + s_tri * a;
      }
      else if (morph < 0.666667f)
      {
        const float a = (morph - 0.333333f) * 3.0f;
        return s_tri * (1.0f - a) + s_saw * a;
      }
      else
      {
        const float a = (morph - 0.666667f) * 3.0f;
        return s_saw * (1.0f - a) + s_sq * a;
      }
    }
  }
}
