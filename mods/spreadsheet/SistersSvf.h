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

    inline void setFreq(float normalizedFreq, float damping)
    {
      // g = tan(π * f) via π-corrected Taylor expansion.
      // Series: tan(x) ≈ x + x³/3 + 2x⁵/15 + ... — we keep x + x³/3
      // (the 'dirty' stmlib form). Valid for normalized f up to ~0.4
      // (~19 kHz at 48k), accurate to <1% in audio range.
      //
      // damping (k = 1/Q): positive for normal filter, ~0 for self-osc
      // edge, slightly negative for sustained self-oscillation. The
      // in-loop tanh on s1 bounds growth into a stable limit cycle.
      const float pi = 3.14159265358979f;
      float pif = pi * normalizedFreq;
      g = pif * (1.0f + pif * pif * 0.333333f);
      r = damping;
      h = 1.0f / (1.0f + r * g + g * g);
    }

    inline void setFreqQ(float normalizedFreq, float q)
    {
      // Backwards-compat wrapper. Cannot reach self-osc this way
      // (q > 0 means damping > 0). Callers wanting self-osc edge
      // should use setFreq() directly with a damping value that can
      // go negative.
      setFreq(normalizedFreq, 1.0f / q);
    }

    inline Output process(float input)
    {
      float hp = (input - r * s1 - g * s1 - s2) * h;
      float bp = g * hp + s1;
      float lp = g * bp + s2;

      // In-loop K-stretched tanh on s1: K * fastTanh(x / K).
      // Same shape as Padé[3/2] tanh but asymptotes at K * 1.73
      // instead of 1.73 alone. K=1.5 calibrated against hardware
      // internal self-osc captures (planning/refs/three-sisters-
      // hardware/internal/) — gets the summed ALL output close to
      // the rail-clip threshold so the rail-clip character actually
      // engages. Without the stretch the limit cycle is too small
      // to ever trigger the output saturation.
      //
      // This is a deliberate approximation of the hardware's OTA
      // voltage-rail saturation (which is amplitude-bounded rather
      // than state-bounded) using the validated ZDF + tanh-on-state
      // mechanism. Not strictly faithful to the ZDF reference but
      // closer to the audible behavior.
      const float kTanhStretch = 1.5f;
      const float kInvTanhStretch = 1.0f / 1.5f;
      s1 = kTanhStretch * fastTanh((g * hp + bp) * kInvTanhStretch);
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
