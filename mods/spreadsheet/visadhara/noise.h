#pragma once

// 7th oscillator: LCG noise + octave decimation. Per the BIA technical
// manual the noise oscillator is pitch-tracked — its sample-and-hold
// rate steps in octaves with the base pitch. We compute the decimation
// stride from the same baseFreq that drives the tonal voices.
//
// Header-only inline per feedback_no_out_of_line_virtuals.

#include <stdint.h>

namespace stolmine
{
  namespace visadhara_noise
  {
    // LCG (Numerical Recipes constants) — fast, sufficient for percussive
    // noise. Per-instance state lives in Visadhara::Internal.
    static inline uint32_t lcg_step(uint32_t state)
    {
      return state * 1103515245u + 12345u;
    }

    // Convert LCG state to bipolar [-1, +1] float.
    static inline float lcg_sample(uint32_t state)
    {
      // Use top 16 bits — lower bits of LCGs have weak randomness.
      const uint32_t bits = (state >> 16) & 0xFFFFu;
      return (float)bits * (1.0f / 32768.0f) - 1.0f;
    }

    // Compute the sample-and-hold stride from baseFreq. The noise osc
    // is octaves above/below a reference: at ref freq, stride = 1
    // (sample every frame); at ref/2, stride = 2; at ref*2, stride = 1
    // (still 1, can't go faster than per-sample). We use 220 Hz as the
    // mid reference — matches the Bass/Alto octave of the V/Oct.
    static inline int stride_for_freq(float baseFreq, float sampleRate)
    {
      const float kRef = 220.0f;
      if (baseFreq <= 0.0f) return (int)(sampleRate / kRef);
      // stride ≈ sampleRate / baseFreq, quantized to powers of 2 for
      // octave-stepping behavior.
      const float ideal = sampleRate / baseFreq;
      // Clamp to [1, 4096] then snap to nearest power of 2 below.
      int s = (int)ideal;
      if (s < 1) s = 1;
      if (s > 4096) s = 4096;
      // Snap to next-lower power of 2.
      int p = 1;
      while (p * 2 <= s) p *= 2;
      return p;
    }
  }
}
