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

      // Tanh saturation on integrator state updates
      // Models OTA current limiting - bounds energy in feedback path
      s1 = fastTanh(g * hp + bp);
      s2 = fastTanh(g * bp + lp);

      return {lp, bp, hp};
    }

    inline void reset()
    {
      s1 = 0.0f;
      s2 = 0.0f;
    }

    // Scaled soft saturation: allows integrator states up to ~4.0
    // before compression, enabling self-oscillation while still
    // preventing the runaway that causes digital clipping
    static inline float fastTanh(float x)
    {
      const float scale = 4.0f;
      const float inv = 1.0f / scale;
      float xs = x * inv;
      return scale * xs / (1.0f + fabsf(xs));
    }
  };

} // namespace stolmine
