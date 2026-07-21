#include "Moire.h"
#include "DrumVoiceSineLUT.h"   // static half-sine LUT (no runtime trig on am335x)
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <cstdint>

// no-tree-vectorize is load-bearing on am335x (feedback_disable_tree_vectorize_am335x).
#pragma GCC optimize("no-tree-vectorize")

namespace stolmine
{
  // The moving intermod lattice: f(h,k) = fc*(h + k*r). v0 fixes a 15-partial odd-harmonic
  // set; r (Spread) is the playable, audio-rate detune that moves the whole field.
  //
  // At r=0 the k-siblings collapse onto their harmonic (phases start aligned -> a clean odd-
  // harmonic tone). As r opens they fan into sidebands and beat; the sub-modes (k<0) dip
  // toward DC and reflect back up in |frequency|.
  static const int NM = 15;
  static const int kH[NM] = {1, 1, 1, 1, 1,  3, 3, 3, 3, 3,  5, 5, 5, 5, 5};
  static const int kK[NM] = {-2, -1, 0, 1, 2,  -2, -1, 0, 1, 2,  -2, -1, 0, 1, 2};
  // Simple rolloff (carrier loudest, falling with h and |k|). NOT the Trinity fit - this is
  // where the original voice diverges. Tune by ear.
  static const float kAmp[NM] = {
    0.30f, 0.50f, 1.00f, 0.50f, 0.30f,   // h=1 family
    0.20f, 0.30f, 0.50f, 0.30f, 0.20f,   // h=3 family
    0.12f, 0.20f, 0.30f, 0.20f, 0.12f};  // h=5 family

  // Per-partial noise for the Drift "life" layer. The Trinity RE proved noise-FM is what
  // makes the hardware feel alive (grit = common-mode noise-FM); Moire diverges - each
  // partial gets its OWN slow drift, so the lattice shimmers and the beating evolves
  // instead of repeating. xorshift -> [-1,1).
  static inline float noise(uint32_t &s)
  {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (float)(int32_t)s * 4.6566129e-10f;   // /2^31
  }

  static inline float sineLUT(float phase)   // phase in [0,1) -> sin(2*pi*phase)
  {
    phase -= floorf(phase);
    bool neg = phase >= 0.5f;
    float ph = neg ? (phase - 0.5f) : phase;
    float idx = ph * 512.0f;
    int i = (int)idx;
    float fr = idx - (float)i;
    float s = kDrumVoiceSineLUT[i] + fr * (kDrumVoiceSineLUT[i + 1] - kDrumVoiceSineLUT[i]);
    return neg ? -s : s;
  }

  struct Moire::Internal
  {
    float phase[NM];
    float driftLp[NM];          // per-partial lowpassed drift (slow random walk)
    uint32_t rng[NM];           // per-partial independent noise stream
    float yPrev = 0.0f;         // last sample's bounded output (for feedback FM)
    Internal()
    {
      for (int m = 0; m < NM; m++)
      {
        phase[m] = 0.0f;
        driftLp[m] = 0.0f;
        rng[m] = 0x9e3779b9u + 0x6d2b79f5u * (uint32_t)(m + 1);   // distinct seeds
      }
    }
  };

  Moire::Moire()
  {
    addInput(mVOct);
    addInput(mSpread);
    addInput(mDrift);
    addInput(mCouple);
    addInput(mDrive);
    addInput(mSync);
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
    float *drift = mDrift.buffer();
    float *couple = mCouple.buffer();
    float *drive = mDrive.buffer();
    float *sync = mSync.buffer();
    float *out = mOut.buffer();

    Internal &I = *mpInternal;
    float sr = globalConfig.sampleRate;
    float invSr = 1.0f / sr;
    float nyq = sr * 0.5f;
    float f0 = CLAMP(8.0f, 8000.0f, mF0.value());
    float level = mLevel.value();
    const float kNorm = 0.25f;
    // Drift lowpass: ~5 Hz random-walk rate per partial. kDriftCents caps the pitch wobble
    // at Drift=1 (0.04 = ~+/- a semitone), enough to keep the beating alive without vibrato.
    const float kDriftHz = 5.0f;
    float driftCoeff = 1.0f - expf(-6.2832f * kDriftHz / sr);
    // Lowpassing white noise shrinks its amplitude (~sqrt(coeff/2)); normalise so driftLp
    // swings ~+/-1, else the pitch wander is ~0.07% (inaudible). Same fix as Tessera's grit.
    float driftNorm = 1.0f / __builtin_sqrtf(driftCoeff / (2.0f - driftCoeff));
    const float kDriftCents = 0.04f;
    const float kInvNM = 1.0f / (float)NM;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float fc = f0 * powf(2.0f, voct[i]);
      float r = CLAMP(0.0f, 2.0f, spread[i]);
      float dr = CLAMP(0.0f, 1.0f, drift[i]) * kDriftCents;
      float cp = CLAMP(0.0f, 1.0f, couple[i]);
      float sy = CLAMP(0.0f, 1.0f, sync[i]);
      float dv = CLAMP(0.0f, 1.0f, drive[i]);

      // Coupled FM: every partial is phase-modulated by the previous sample's summed output
      // (1-sample feedback = the whole lattice modulating itself). Deviation up to fc.
      float fmDev = cp * fc * I.yPrev;

      float y = 0.0f;
      bool prevWrapped = false;
      for (int m = 0; m < NM; m++)
      {
        // per-partial slow independent drift -> evolving beating (the "life" layer)
        I.driftLp[m] += driftCoeff * (noise(I.rng[m]) - I.driftLp[m]);
        float f = fc * ((float)kH[m] + (float)kK[m] * r) * (1.0f + I.driftLp[m] * driftNorm * dr);

        // Cascading hard sync: partial m resets when the partial below it wrapped, once the
        // sync amount reaches this link (cascade grows up the lattice, snapping partials onto
        // the fundamental's period -> harmonic reinforcement).
        if (m > 0 && prevWrapped && sy > (float)m * kInvNM)
          I.phase[m] = 0.0f;

        I.phase[m] += (f + fmDev) * invSr;   // signed advance + coupled FM
        bool wrapped = false;
        if (I.phase[m] >= 1.0f || I.phase[m] < 0.0f)
        {
          I.phase[m] -= floorf(I.phase[m]);
          wrapped = true;
        }
        float fa = f < 0.0f ? -f : f;
        if (fa >= 20.0f && fa < nyq)
          y += kAmp[m] * sineLUT(I.phase[m]);
        prevWrapped = wrapped;
      }

      // bounded feedback term for next sample's FM (keeps the loop stable)
      float ys = y * kNorm;
      I.yPrev = ys / __builtin_sqrtf(1.0f + ys * ys);

      // Drive: pre-output saturation (up to 12x into the softclip = thickness/limiting).
      float yd = ys * (1.0f + dv * 11.0f);
      out[i] = (yd / __builtin_sqrtf(1.0f + yd * yd)) * level;
    }
  }

} // namespace stolmine
