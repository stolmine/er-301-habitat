#pragma once

// Custom ZDF SVF for Three Sisters topology. Returns LP/BP/HP
// simultaneously per sample for cascaded filter routings.
//
// Phase 3 DSP fixes vs the biome carbon copy:
//
//  Issue #1 (π bug): coefficient computation was Taylor of tan(x) but
//  missing the leading π — every cutoff was 3.14× flat. Fixed by
//  applying π factor before the Taylor expansion.
//
//  Issue #3 (self-osc): the prior softClip on integrator states could
//  only attenuate (a loss). Real Three Sisters self-oscillates cleanly
//  at high Q. Replaced with in-loop tanh on the bp integrator state
//  (s1) — bounds the limit cycle so the filter sings instead of decaying.
//  s2 stays unclipped per the validated reference model.
//
//  Issue #4 subsumed by #3: saturation now sits inside the resonance
//  feedback loop, not on output of integrator states.

#include <math.h>

namespace stolmine
{

  struct SistersSvf
  {
    float s1 = 0.0f; // integrator 1 state (bp-related)
    float s2 = 0.0f; // integrator 2 state (lp-related)
    float g = 0.0f;  // frequency coefficient = tan(π * f) approx
    float r = 0.0f;  // damping (1/Q)
    float h = 0.0f;  // precomputed 1/(1 + r*g + g*g)

    struct Output
    {
      float lp, bp, hp;
    };

    inline void setFreqQ(float normalizedFreq, float q)
    {
      // g = tan(π * f) via π-corrected Taylor expansion.
      // Series: tan(x) ≈ x + x³/3 + 2x⁵/15 + ... — we keep x + x³/3
      // (the 'dirty' stmlib form). Valid for normalized f up to ~0.4
      // (~19 kHz at 48k), accurate to <1% in audio range.
      const float pi = 3.14159265358979f;
      float pif = pi * normalizedFreq;
      g = pif * (1.0f + pif * pif * 0.333333f);
      r = 1.0f / q;
      h = 1.0f / (1.0f + r * g + g * g);
    }

    inline Output process(float input)
    {
      float hp = (input - r * s1 - g * s1 - s2) * h;
      float bp = g * hp + s1;
      float lp = g * bp + s2;

      // In-loop tanh on the bp integrator state. At low Q this is
      // near-linear (Padé Tanh accurate <0.5% for |x| < 3). At high
      // Q the resonant state would otherwise grow unboundedly; tanh
      // bounds it to a stable limit cycle, producing self-oscillation
      // with near-sinusoidal character. s2 stays unclipped per the
      // reference model (clipping both states damages stability and
      // tone).
      s1 = fastTanh(g * hp + bp);
      s2 = g * bp + lp;

      return {lp, bp, hp};
    }

    inline void reset()
    {
      s1 = 0.0f;
      s2 = 0.0f;
    }

    // Padé[3/2] tanh approximation: x * (27 + x²) / (27 + 9x²)
    // 4 multiplies + 1 divide ≈ ~35 cycles on Cortex-A8.
    // Accurate to ~0.5% for |x| < 3; smoothly saturates beyond.
    static inline float fastTanh(float x)
    {
      float x2 = x * x;
      return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
  };

} // namespace stolmine
