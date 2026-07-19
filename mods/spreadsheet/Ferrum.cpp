#include "Ferrum.h"
#include "DrumVoiceSineLUT.h"   // static half-sine LUT (no runtime trig on am335x)
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

// no-tree-vectorize is load-bearing on am335x (feedback_disable_tree_vectorize_am335x,
// TOP PRIORITY). Keeps the per-sample FM loop off trapping quad-D NEON.
#pragma GCC optimize("no-tree-vectorize")

namespace stolmine
{

  // Shape = quantized integer FM ratio (measured: ratio snaps between harmonics).
  static const float kRatios[8] = {1, 2, 3, 4, 5, 6, 7, 8};

  // Full-period sine from the half-sine LUT: phase in [0,1) -> sin(2*pi*phase).
  static inline float sineLUT(float phase)
  {
    phase -= floorf(phase);
    bool neg = phase >= 0.5f;
    float ph = neg ? (phase - 0.5f) : phase;   // [0,0.5)
    float idx = ph * 512.0f;                    // [0,256)
    int i = (int)idx;
    float fr = idx - (float)i;
    float s = kDrumVoiceSineLUT[i] + fr * (kDrumVoiceSineLUT[i + 1] - kDrumVoiceSineLUT[i]);
    return neg ? -s : s;
  }

  static inline uint32_t lcg(uint32_t &s) { s = s * 1103515245u + 12345u; return s; }
  static inline float noise(uint32_t &s) { return (float)((lcg(s) >> 9) & 0xFFFF) / 32768.0f - 1.0f; }

  struct Ferrum::Internal
  {
    float cph = 0, mph = 0, prevC = 0;   // carrier phase, modulator phase, feedback
    float pitchEnv = 0, ampEnv = 0;
    int holdLeft = 0;
    float prevTrig = 0;
    // latched at trigger (drum pitch/ratio/sweep are set at the hit):
    float baseHz = 110, ratio = 2, sweepDepth = 0, pitchCoeff = 0.999f, velAmp = 1;
    uint32_t rng = 0x2ab41fu;
  };

  Ferrum::Ferrum()
  {
    addInput(mTrigger);
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mPitch);
    addParameter(mCharacter);
    addParameter(mShape);
    addParameter(mGrit);
    addParameter(mSweep);
    addParameter(mTime);
    addParameter(mHold);
    addParameter(mDecay);
    addParameter(mLevel);
    mpInternal = new Internal();
  }

  Ferrum::~Ferrum() { delete mpInternal; }

  float Ferrum::getEnvLevel() { return mpInternal->ampEnv; }

  void Ferrum::process()
  {
    float *trig = mTrigger.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();
    Internal &I = *mpInternal;
    float sr = globalConfig.sampleRate;

    float character = CLAMP(0.0f, 1.0f, mCharacter.value());
    float grit = CLAMP(0.0f, 1.0f, mGrit.value());
    float level = CLAMP(0.0f, 1.0f, mLevel.value());
    float decay = CLAMP(0.0f, 1.0f, mDecay.value());
    float hold = CLAMP(0.0f, 1.0f, mHold.value());
    float timeK = CLAMP(0.0f, 1.0f, mTime.value());
    float pitchP = CLAMP(0.0f, 1.0f, mPitch.value());
    float shapeP = CLAMP(0.0f, 1.0f, mShape.value());
    float sweepP = CLAMP(0.0f, 1.0f, mSweep.value());

    // block-rate derived coefficients (live params). Character = FM mod depth with a
    // floor (some brightness even at 0) that ramps into operator feedback at the top -
    // tuned to the measured Trinity Character sweep (centroid 390 -> 2958).
    float modDepth = 0.5f + character * 3.0f;            // FM index (cycles)
    float fbChar = character > 0.7f ? (character - 0.7f) / 0.3f * 0.3f : 0.0f;  // feedback regime
    float fbGrit = (grit < 0.7f ? grit : 0.7f) / 0.7f * 0.45f;
    float fb = fbChar + fbGrit;
    float noiseBlend = grit > 0.55f ? (grit - 0.55f) / 0.45f : 0.0f;
    float tauD = 0.003f * powf(87.0f, decay);            // amp decay 3 ms .. ~260 ms+
    float ampCoeff = expf(-1.0f / (tauD * sr));
    int holdSamples = (int)(hold * 0.8f * sr);           // 0 .. 800 ms
    float tauP = 0.002f + timeK * 0.35f;                 // pitch-env 2 ms .. 350 ms
    float pitchCoeff = expf(-1.0f / (tauP * sr));

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float tv = trig[i];
      if (tv > 0.5f && I.prevTrig <= 0.5f)   // rising edge -> new hit
      {
        // ~40 Hz .. 640 Hz over 4 oct + V/oct (matches Trinity carrier ~41 Hz at note 48)
        I.baseHz = 40.0f * powf(2.0f, pitchP * 4.0f + voct[i]);
        int ri = (int)(shapeP * 7.999f);
        I.ratio = kRatios[ri];
        I.sweepDepth = sweepP * 24.0f;
        I.pitchCoeff = pitchCoeff;
        I.pitchEnv = 1.0f; I.ampEnv = 1.0f; I.holdLeft = holdSamples;
        I.cph = 0.0f; I.mph = 0.0f; I.prevC = 0.0f; I.velAmp = 1.0f;
      }
      I.prevTrig = tv;

      // pitch envelope: f = base*(1 + depth*env), env decays 1 -> 0
      float f = I.baseHz * (1.0f + I.sweepDepth * I.pitchEnv);
      I.pitchEnv *= I.pitchCoeff;
      float mf = I.ratio * f;

      // 2-op FM with operator feedback
      float modOut = sineLUT(I.mph);
      float carrier = sineLUT(I.cph + modDepth * modOut + fb * I.prevC);
      I.prevC = carrier;
      I.cph += f / sr;  I.cph -= floorf(I.cph);
      I.mph += mf / sr; I.mph -= floorf(I.mph);

      // Grit: crossfade to broadband noise past ~0.55 (the noise-impact oscillator)
      if (noiseBlend > 0.0f)
        carrier = carrier * (1.0f - noiseBlend) + noise(I.rng) * noiseBlend;

      // amp envelope: instant attack, Hold plateau, exp Decay
      if (I.holdLeft > 0) I.holdLeft--; else I.ampEnv *= ampCoeff;

      out[i] = carrier * I.ampEnv * I.velAmp * level;
    }
  }

} // namespace stolmine
