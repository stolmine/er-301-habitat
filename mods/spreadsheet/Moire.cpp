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

  // Interaction topology. The partials are a NETWORK with roles, not a uniform bank. The
  // carrier (m0) stays clean as the reference; the rest interleave COUPLE / LOCK so the two
  // effects target complementary partials and spread across the spectrum (staggered pattern).
  //   COUPLE (1): phase-modulated by a specific partner partial (structured FM, not global).
  //   LOCK   (2): frequency snapped toward the nearest harmonic of fc (crystalline, alias-free).
  static const int kRole[NM] = {0, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2};
  // Progressive disclosure: within each group, partials fade in ONE AT A TIME as the control
  // rises past their threshold (the coupled/locked COUNT grows; it is not a global depth).
  static const float kActT[NM] = {
    0.000f, 0.000f, 0.000f, 0.143f, 0.143f, 0.286f, 0.286f, 0.429f,
    0.429f, 0.571f, 0.571f, 0.714f, 0.714f, 0.857f, 0.857f};

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
    float sinePrev[NM];         // each partial's last sine output (structured FM modulators)
    uint32_t rng[NM];           // per-partial independent noise stream
    Internal()
    {
      for (int m = 0; m < NM; m++)
      {
        phase[m] = 0.0f;
        driftLp[m] = 0.0f;
        sinePrev[m] = 0.0f;
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
    const float kFmMax = 0.5f;      // couple FM index (deviation up to 0.5*fc per pair)

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float fc = f0 * powf(2.0f, voct[i]);
      float r = CLAMP(0.0f, 2.0f, spread[i]);
      float dr = CLAMP(0.0f, 1.0f, drift[i]) * kDriftCents;
      float cp = CLAMP(0.0f, 1.0f, couple[i]);
      float sy = CLAMP(0.0f, 1.0f, sync[i]);
      float dv = CLAMP(0.0f, 1.0f, drive[i]);

      float y = 0.0f;
      for (int m = 0; m < NM; m++)
      {
        // per-partial slow independent drift -> evolving beating (the "life" layer)
        I.driftLp[m] += driftCoeff * (noise(I.rng[m]) - I.driftLp[m]);
        float f = fc * ((float)kH[m] + (float)kK[m] * r) * (1.0f + I.driftLp[m] * driftNorm * dr);

        int role = kRole[m];
        float fmAdd = 0.0f;
        if (role == 1)
        {
          // COUPLE: structured FM by the partner partial below (its last sine output), faded
          // in progressively so the coupled COUNT grows as the control rises. Not global.
          float act = CLAMP(0.0f, 1.0f, (cp - kActT[m]) * 7.0f);
          fmAdd = act * kFmMax * fc * I.sinePrev[m - 1];
        }
        else if (role == 2)
        {
          // LOCK: snap the frequency toward the nearest harmonic of fc (crystalline, alias-
          // free), progressively. Pulls the inharmonic lattice back onto structure.
          float act = CLAMP(0.0f, 1.0f, (sy - kActT[m]) * 7.0f);
          if (act > 0.0f)
          {
            float hn = floorf(f / fc + 0.5f);
            if (hn < 1.0f) hn = 1.0f;
            f = f + (hn * fc - f) * act;
          }
        }

        I.phase[m] += (f + fmAdd) * invSr;   // signed advance + structured FM
        I.phase[m] -= floorf(I.phase[m]);

        float s = sineLUT(I.phase[m]);
        I.sinePrev[m] = s;                   // modulator source for coupled partners
        float fa = f < 0.0f ? -f : f;
        if (fa >= 20.0f && fa < nyq)
          y += kAmp[m] * s;
      }

      // Drive: light glue only (up to ~4x into the softclip). Real weight is meant to
      // accumulate through the resonant filter matrix (next step), not brute saturation.
      float ys = y * kNorm;
      float yd = ys * (1.0f + dv * 3.0f);
      out[i] = (yd / __builtin_sqrtf(1.0f + yd * yd)) * level;
    }
  }

} // namespace stolmine
