#include "Moire.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <cstdint>

// no-tree-vectorize is load-bearing on am335x (feedback_disable_tree_vectorize_am335x).
#pragma GCC optimize("no-tree-vectorize")

namespace stolmine
{
  // The moving intermod lattice f(h,k)=fc*(h+k*r) now sets the RESONANT frequencies of a bank
  // of 2-pole bandpass resonators (TPT/Zavalishin SVF). Driven by shared noise + network
  // feedback, they ring: body and weight come from resonance, not from summed sines.
  static const int NM = 15;
  static const int kH[NM] = {1, 1, 1, 1, 1,  3, 3, 3, 3, 3,  5, 5, 5, 5, 5};
  static const int kK[NM] = {-2, -1, 0, 1, 2,  -2, -1, 0, 1, 2,  -2, -1, 0, 1, 2};
  // per-resonator output gain (carrier loudest, falling with h and |k|)
  static const float kAmp[NM] = {
    0.30f, 0.50f, 1.00f, 0.50f, 0.30f,
    0.20f, 0.30f, 0.50f, 0.30f, 0.20f,
    0.12f, 0.20f, 0.30f, 0.20f, 0.12f};

  // xorshift -> [-1,1); one shared exciter stream + per-resonator drift streams.
  static inline float noise(uint32_t &s)
  {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (float)(int32_t)s * 4.6566129e-10f;
  }

  // tan(pi*f/fs) for the SVF prewarp, cheap poly (no libm), good to ~fs/6; clamped above.
  static inline float tanApprox(float w)
  {
    if (w > 1.3f) w = 1.3f;
    float w2 = w * w;
    return w * (1.0f + w2 * (0.33333f + w2 * 0.13333f));
  }

  static inline float softclip(float x) { return x / __builtin_sqrtf(1.0f + x * x); }

  struct Moire::Internal
  {
    float ic1[NM], ic2[NM];     // SVF integrator states (band/low)
    float bp[NM];               // last bandpass output (for coupling feedback)
    float driftLp[NM];
    uint32_t rng[NM];
    uint32_t exRng = 0x1234567u; // shared exciter noise
    float fbPrev = 0.0f;        // bounded network feedback from last sample
    Internal()
    {
      for (int m = 0; m < NM; m++)
      {
        ic1[m] = ic2[m] = 0.0f;
        bp[m] = 0.0f;
        driftLp[m] = 0.0f;
        rng[m] = 0x9e3779b9u + 0x6d2b79f5u * (uint32_t)(m + 1);
      }
    }
  };

  Moire::Moire()
  {
    addInput(mVOct);
    addInput(mSpread);
    addInput(mBody);
    addInput(mAir);
    addInput(mCouple);
    addInput(mDrift);
    addInput(mLock);
    addOutput(mOut);
    addParameter(mF0);
    addParameter(mLevel);
    mpInternal = new Internal;
  }

  Moire::~Moire() { delete mpInternal; }

  void Moire::process()
  {
    float *voct = mVOct.buffer();
    float *spread = mSpread.buffer();
    float *body = mBody.buffer();
    float *air = mAir.buffer();
    float *couple = mCouple.buffer();
    float *drift = mDrift.buffer();
    float *lock = mLock.buffer();
    float *out = mOut.buffer();

    Internal &I = *mpInternal;
    float sr = globalConfig.sampleRate;
    float piOverSr = 3.14159265f / sr;
    float nyq = sr * 0.5f;
    float f0 = CLAMP(8.0f, 8000.0f, mF0.value());
    float level = mLevel.value();
    const float kDriftHz = 5.0f;
    float driftCoeff = 1.0f - expf(-6.2832f * kDriftHz / sr);
    float driftNorm = 1.0f / __builtin_sqrtf(driftCoeff / (2.0f - driftCoeff));
    const float kDriftCents = 0.04f;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float fc = f0 * powf(2.0f, voct[i]);
      float r = CLAMP(0.0f, 2.0f, spread[i]);
      float dr = CLAMP(0.0f, 1.0f, drift[i]) * kDriftCents;
      float bd = CLAMP(0.0f, 1.0f, body[i]);
      float ar = CLAMP(0.0f, 1.0f, air[i]);
      float cp = CLAMP(0.0f, 1.0f, couple[i]);
      float lk = CLAMP(0.0f, 1.0f, lock[i]);

      // Body -> SVF damping k = 1/Q. bd 0 = broad/breathy (k~1.4), bd 1 = sharp ring (k~0.02).
      float k = 1.4f - bd * 1.38f;
      // A bandpass fed by white noise outputs RMS ~1/sqrt(k), so higher Q would collapse in
      // level; compensate the other way so turning Body up brings the RING FORWARD. Bounded
      // by the output softclip.
      float gainComp = __builtin_sqrtf(0.6f / (k + 0.01f));

      // Shared excitation: noise (Air) + bounded network feedback (Couple).
      float exNoise = noise(I.exRng) * ar;
      float exFb = I.fbPrev * cp;
      float ex = exNoise + exFb;

      float y = 0.0f;
      float bpSum = 0.0f;
      for (int m = 0; m < NM; m++)
      {
        I.driftLp[m] += driftCoeff * (noise(I.rng[m]) - I.driftLp[m]);
        float f = fc * ((float)kH[m] + (float)kK[m] * r) * (1.0f + I.driftLp[m] * driftNorm * dr);
        float fa = f < 0.0f ? -f : f;
        // Lock: snap toward nearest harmonic of fc (crystalline reinforcement).
        if (lk > 0.0f)
        {
          float hn = floorf(fa / fc + 0.5f);
          if (hn < 1.0f) hn = 1.0f;
          fa = fa + (hn * fc - fa) * lk;
        }
        if (fa < 20.0f) fa = 20.0f;
        if (fa > nyq * 0.98f) fa = nyq * 0.98f;

        // TPT/Cytomic 2-pole SVF, bandpass tap.
        float g = tanApprox(fa * piOverSr);
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        float a3 = g * a2;
        float v3 = ex - I.ic2[m];
        float v1 = a1 * I.ic1[m] + a2 * v3;
        float v2 = I.ic2[m] + a2 * I.ic1[m] + a3 * v3;
        I.ic1[m] = 2.0f * v1 - I.ic1[m];
        I.ic2[m] = 2.0f * v2 - I.ic2[m];
        float bpv = v1;                 // bandpass output rings at fa
        I.bp[m] = bpv;
        bpSum += bpv;
        y += kAmp[m] * bpv;
      }

      // Feed the (bounded) network sum back for next sample's coupling excitation.
      I.fbPrev = softclip(bpSum * 0.5f);

      out[i] = softclip(y * gainComp) * level;
    }
  }

} // namespace stolmine
