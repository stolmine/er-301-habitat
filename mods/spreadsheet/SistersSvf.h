#pragma once

// Custom ZDF SVF with tanh-saturating integrators (OTA-style nonlinearity)
// Returns LP/BP/HP simultaneously per sample for cascaded filter topologies.

#include <math.h>

namespace stolmine
{

  struct SistersSvf
  {
    float s1 = 0.0f; // integrator 1 state
    float s2 = 0.0f; // integrator 2 state
    float g = 0.0f;  // frequency coefficient
    float r = 0.0f;  // damping (1/Q)
    float h = 0.0f;  // precomputed 1/(1 + r*g + g*g)

    struct Output
    {
      float lp, bp, hp;
    };

    inline void setFreqQ(float normalizedFreq, float q)
    {
      // tan approximation (matches stmlib FREQUENCY_DIRTY)
      float f = normalizedFreq;
      g = f * (1.0f + f * f * 0.333333f);
      r = 1.0f / q;
      h = 1.0f / (1.0f + r * g + g * g);
    }

    inline Output process(float input)
    {
      float hp = (input - r * s1 - g * s1 - s2) * h;
      float bp = g * hp + s1;
      float lp = g * bp + s2;

      // State updates - saturation applied gently to prevent runaway
      // without killing resonance
      float newS1 = g * hp + bp;
      float newS2 = g * bp + lp;

      // Soft clip only when states get large (>2.0)
      // Below that, filter behaves like a pure linear SVF
      s1 = softClip(newS1);
      s2 = softClip(newS2);

      return {lp, bp, hp};
    }

    inline void reset()
    {
      s1 = 0.0f;
      s2 = 0.0f;
    }

    // Soft clip: linear below threshold, smoothly compressed above.
    // Preserves filter dynamics at normal levels, prevents blowup at extremes.
    // At |x| < 2: output ~= x (nearly transparent)
    // At |x| > 2: output compresses toward +/-4
    static inline float softClip(float x)
    {
      if (x > 2.0f)
        return 2.0f + (x - 2.0f) / (1.0f + (x - 2.0f) * 0.5f);
      else if (x < -2.0f)
        return -2.0f + (x + 2.0f) / (1.0f - (x + 2.0f) * 0.5f);
      return x;
    }
  };

} // namespace stolmine
