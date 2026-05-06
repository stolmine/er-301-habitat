#pragma once

// 3-op closed-loop phase-modulation operator for Visadhara Metal mode.
// Topology per the BIA technical manual: "A pair of 3-operator
// phase-modulated oscillators for producing metallic, noisy, and alien
// sounds." Each pair is one 3-op voice; Visadhara runs two pairs in
// parallel and sums their outputs.
//
// Topology:
//   op1: phase += inc1; out1 = sin(phase + fb * lastOut1)        (self-fb)
//   op2: phase += inc2; out2 = sin(phase + mod12 * lastOut1)
//   op3: phase += inc3; out3 = sin(phase + mod23 * lastOut2)
//   voice_out = out3
//
// Header-only inline per feedback_no_out_of_line_virtuals. Reuses
// visadhara_morph::poly_sin (Bhaskara polynomial) — no libm trig per
// feedback_package_trig_lut.

#include "morph.h"
#include <math.h>

namespace stolmine
{
  namespace visadhara_pmm
  {
    struct Voice
    {
      float phase[3];
      float lastOut[3];
    };

    static inline void reset(Voice &v)
    {
      v.phase[0] = 0.0f;
      v.phase[1] = 0.0f;
      v.phase[2] = 0.0f;
      v.lastOut[0] = 0.0f;
      v.lastOut[1] = 0.0f;
      v.lastOut[2] = 0.0f;
    }

    // Wrap phase + modulation back to [0, 1) for poly_sin. Handles
    // negative arguments (modulation can drive phase negative).
    static inline float wrap01(float p)
    {
      return p - floorf(p);
    }

    // Run one sample of the 3-op chain. Phase increments inc1/2/3 are
    // per-sample (cycles per sample = freq / sampleRate). fb is op1
    // self-feedback; mod12 modulates op2 by op1; mod23 modulates op3 by
    // op2. morphW is precomputed block-rate equal-power crossfade
    // weights for the per-operator waveshape (sine→tri→saw→sq). Output
    // is op3.
    static inline float tick(Voice &v,
                              float inc1, float inc2, float inc3,
                              float fb, float mod12, float mod23,
                              const visadhara_morph::Weights &morphW)
    {
      // op1 — self-feedback
      v.phase[0] += inc1;
      if (v.phase[0] >= 1.0f) v.phase[0] -= floorf(v.phase[0]);
      const float p1 = wrap01(v.phase[0] + fb * v.lastOut[0]);
      v.lastOut[0] = visadhara_morph::sample_w(p1, morphW);

      // op2 — modulated by op1
      v.phase[1] += inc2;
      if (v.phase[1] >= 1.0f) v.phase[1] -= floorf(v.phase[1]);
      const float p2 = wrap01(v.phase[1] + mod12 * v.lastOut[0]);
      v.lastOut[1] = visadhara_morph::sample_w(p2, morphW);

      // op3 — modulated by op2
      v.phase[2] += inc3;
      if (v.phase[2] >= 1.0f) v.phase[2] -= floorf(v.phase[2]);
      const float p3 = wrap01(v.phase[2] + mod23 * v.lastOut[1]);
      v.lastOut[2] = visadhara_morph::sample_w(p3, morphW);

      return v.lastOut[2];
    }
  }
}
