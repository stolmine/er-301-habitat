#include "Moire.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <cstdint>

// no-tree-vectorize is load-bearing on am335x (feedback_disable_tree_vectorize_am335x).
#pragma GCC optimize("no-tree-vectorize")

namespace stolmine
{
  // Rain-on-bells scattering network. NM tuned 2-pole resonators (bells) on the moving lattice
  // f(h,k)=fc*(h+k*r). Sparse rain strikes hit random bells; each bell's ring is written into a
  // shared meso-time delay, and per-bell taps read it back tens-to-hundreds of ms later to
  // re-strike other bells - so one drop cascades through the network (Bounce).
  static const int NM = 15;
  static const int kH[NM] = {1, 1, 1, 1, 1,  3, 3, 3, 3, 3,  5, 5, 5, 5, 5};
  static const int kK[NM] = {-2, -1, 0, 1, 2,  -2, -1, 0, 1, 2,  -2, -1, 0, 1, 2};
  static const float kAmp[NM] = {
    0.30f, 0.50f, 1.00f, 0.50f, 0.30f,
    0.20f, 0.30f, 0.50f, 0.30f, 0.20f,
    0.12f, 0.20f, 0.30f, 0.20f, 0.12f};
  // Irregular per-bell tap fractions of the meso-time window (prime-ish, non-repetitive
  // scatter so the cascade never lines up into a flam).
  static const float kTapFrac[NM] = {
    0.31f, 0.52f, 0.73f, 0.41f, 0.88f,
    0.23f, 0.61f, 0.94f, 0.37f, 0.79f,
    0.47f, 0.67f, 0.29f, 0.83f, 0.56f};

  // shared meso-time delay buffer: 0.5 s at 48 k, power-of-2 for mask wrap.
  static const int DBITS = 15;               // 32768 samples ~= 0.68 s @48k
  static const int DSIZE = 1 << DBITS;
  static const int DMASK = DSIZE - 1;

  static inline float noise(uint32_t &s)
  {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (float)(int32_t)s * 4.6566129e-10f;   // [-1,1)
  }
  static inline float rnd01(uint32_t &s)
  {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (float)(s >> 8) * (1.0f / 16777216.0f);  // [0,1)
  }

  static inline float tanApprox(float w)
  {
    if (w > 1.3f) w = 1.3f;
    float w2 = w * w;
    return w * (1.0f + w2 * (0.33333f + w2 * 0.13333f));
  }
  static inline float softclip(float x) { return x / __builtin_sqrtf(1.0f + x * x); }

  struct Moire::Internal
  {
    float ic1[NM], ic2[NM];     // SVF integrator states
    float driftLp[NM];
    uint32_t rng[NM];
    uint32_t exRng = 0x1234567u; // rain RNG
    float *delay;                // shared meso-time scatter buffer (heap)
    int w = 0;                   // delay write head
    Internal()
    {
      for (int m = 0; m < NM; m++)
      {
        ic1[m] = ic2[m] = 0.0f;
        driftLp[m] = 0.0f;
        rng[m] = 0x9e3779b9u + 0x6d2b79f5u * (uint32_t)(m + 1);
      }
      delay = new float[DSIZE];
      for (int i = 0; i < DSIZE; i++) delay[i] = 0.0f;
    }
    ~Internal() { delete[] delay; }
  };

  Moire::Moire()
  {
    addInput(mVOct);
    addInput(mSpread);
    addInput(mBody);
    addInput(mDensity);
    addInput(mBounce);
    addInput(mTime);
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
    float *density = mDensity.buffer();
    float *bounce = mBounce.buffer();
    float *timeb = mTime.buffer();
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
    float maxDelay = (float)(DSIZE - 4);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float fc = f0 * powf(2.0f, voct[i]);
      float r = CLAMP(0.0f, 2.0f, spread[i]);
      float dr = CLAMP(0.0f, 1.0f, drift[i]) * kDriftCents;
      float bd = CLAMP(0.0f, 1.0f, body[i]);
      float dens = CLAMP(0.0f, 1.0f, density[i]);
      float bnc = CLAMP(0.0f, 0.95f, bounce[i]);
      float lk = CLAMP(0.0f, 1.0f, lock[i]);
      // Meso-time window: 20 ms .. 400 ms, exponential.
      float winMs = 20.0f * powf(20.0f, CLAMP(0.0f, 1.0f, timeb[i]));
      float winSamp = winMs * 0.001f * sr;
      if (winSamp > maxDelay) winSamp = maxDelay;

      // Bells ring long: high Q. Body sets the ring time (k = 1/Q).
      float k = 0.5f - bd * 0.49f;                 // 0.5 (short) .. 0.01 (long ring)
      float gainComp = __builtin_sqrtf(0.15f / (k + 0.01f));

      // Rain: Poisson-ish drop, rate 0.5 .. ~600 drops/s (exponential in Density). A drop is a
      // sharp impact written into the delay line, where it scatters to the bells and echoes.
      float rate = 0.5f * powf(1200.0f, dens);
      float pDrop = rate / sr;
      float drop = 0.0f;
      if (rnd01(I.exRng) < pDrop)
        drop = 16.0f + 16.0f * rnd01(I.exRng);   // varied drop weight; kicks high-Q bells

      // Feedback delay carrying the IMPACTS: an impulse recirculates once per meso-window,
      // decaying at Bounce (< 1, always stable) - a finite train of echoing strikes, not a
      // runaway loop. The bells are read-only taps off this line (below).
      int fbOff = (int)winSamp;
      float echo = I.delay[(I.w - fbOff) & DMASK];
      float impact = drop + bnc * echo;
      I.delay[I.w] = impact;

      float bellSum = 0.0f;
      for (int m = 0; m < NM; m++)
      {
        I.driftLp[m] += driftCoeff * (noise(I.rng[m]) - I.driftLp[m]);
        float f = fc * ((float)kH[m] + (float)kK[m] * r) * (1.0f + I.driftLp[m] * driftNorm * dr);
        float fa = f < 0.0f ? -f : f;
        if (lk > 0.0f)
        {
          float hn = floorf(fa / fc + 0.5f);
          if (hn < 1.0f) hn = 1.0f;
          fa = fa + (hn * fc - fa) * lk;
        }
        if (fa < 20.0f) fa = 20.0f;
        if (fa > nyq * 0.98f) fa = nyq * 0.98f;

        // Each bell reads the delay at its own scattered offset (read-only): the impact train
        // arrives at each bell at a different meso-time -> one drop becomes an arpeggiated
        // chime cluster, and every echo generation restrikes the whole set.
        int off = (int)(winSamp * kTapFrac[m]) + 1;
        float ex = I.delay[(I.w - off) & DMASK];

        // 2-pole SVF, bandpass tap.
        float g = tanApprox(fa * piOverSr);
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        float a3 = g * a2;
        float v3 = ex - I.ic2[m];
        float v1 = a1 * I.ic1[m] + a2 * v3;
        float v2 = I.ic2[m] + a2 * I.ic1[m] + a3 * v3;
        I.ic1[m] = 2.0f * v1 - I.ic1[m];
        I.ic2[m] = 2.0f * v2 - I.ic2[m];
        bellSum += kAmp[m] * v1;
      }

      I.w = (I.w + 1) & DMASK;
      out[i] = softclip(bellSum * gainComp * 0.3f) * level;
    }
  }

} // namespace stolmine
