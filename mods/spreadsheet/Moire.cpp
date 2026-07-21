#include "Moire.h"
#include "DrumVoiceSineLUT.h"   // static half-sine LUT (no runtime trig on am335x)
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

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
    Internal() { for (int m = 0; m < NM; m++) phase[m] = 0.0f; }
  };

  Moire::Moire()
  {
    addInput(mVOct);
    addInput(mSpread);
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
    float *out = mOut.buffer();

    Internal &I = *mpInternal;
    float sr = globalConfig.sampleRate;
    float invSr = 1.0f / sr;
    float nyq = sr * 0.5f;
    float f0 = CLAMP(8.0f, 8000.0f, mF0.value());
    float level = mLevel.value();
    const float kNorm = 0.25f;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float fc = f0 * powf(2.0f, voct[i]);
      float r = CLAMP(0.0f, 2.0f, spread[i]);

      float y = 0.0f;
      for (int m = 0; m < NM; m++)
      {
        float f = fc * ((float)kH[m] + (float)kK[m] * r);
        float fa = f < 0.0f ? -f : f;
        if (fa >= 20.0f && fa < nyq)
          y += kAmp[m] * sineLUT(I.phase[m]);
        I.phase[m] += f * invSr;          // signed advance (sub-modes reflect through DC)
        I.phase[m] -= floorf(I.phase[m]);
      }

      // gentle bounded softclip (x/sqrt(1+x^2)); __builtin_sqrtf -> VFP vsqrt.f32.
      float yd = y * kNorm;
      out[i] = (yd / __builtin_sqrtf(1.0f + yd * yd)) * level;
    }
  }

} // namespace stolmine
