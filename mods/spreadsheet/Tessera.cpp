#include "Tessera.h"
#include "DrumVoiceSineLUT.h"   // static half-sine LUT (no runtime trig on am335x)
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

// no-tree-vectorize is load-bearing on am335x (feedback_disable_tree_vectorize_am335x).
#pragma GCC optimize("no-tree-vectorize")

namespace stolmine
{

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

  static inline uint32_t lcg(uint32_t &s) { s = s * 1103515245u + 12345u; return s; }
  static inline float noise(uint32_t &s) { return (float)((lcg(s) >> 9) & 0xFFFF) / 32768.0f - 1.0f; }

  struct Tessera::Internal
  {
    float ph1 = 0, ph2 = 0;
    float pitchEnv = 0, ampEnv = 0, ampEnv2 = 0;
    int holdLeft = 0;
    float prevTrig = 0;
    float baseHz = 110, sweepDepth = 0, pitchCoeff = 0.999f;
    uint32_t rng = 0x51ee7u;
  };

  Tessera::Tessera()
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

  Tessera::~Tessera() { delete mpInternal; }

  float Tessera::getEnvLevel() { return mpInternal->ampEnv; }

  void Tessera::process()
  {
    float *trig = mTrigger.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();
    Internal &I = *mpInternal;
    float sr = globalConfig.sampleRate;

    float character = CLAMP(0.0f, 1.0f, mCharacter.value());
    float shape = CLAMP(0.0f, 1.0f, mShape.value());
    float grit = CLAMP(0.0f, 1.0f, mGrit.value());
    float level = CLAMP(0.0f, 1.0f, mLevel.value());
    float decay = CLAMP(0.0f, 1.0f, mDecay.value());
    float hold = CLAMP(0.0f, 1.0f, mHold.value());
    float timeK = CLAMP(0.0f, 1.0f, mTime.value());
    float pitchP = CLAMP(0.0f, 1.0f, mPitch.value());
    float sweepP = CLAMP(0.0f, 1.0f, mSweep.value());

    // amp decay shortens past ~0.75 grit (measured 885 -> ~260 ms) -> 808 snap
    float gritShorten = grit > 0.75f ? 1.0f - (grit - 0.75f) / 0.25f * 0.7f : 1.0f;
    float tauD = 0.003f * powf(87.0f, decay) * gritShorten;
    float ampCoeff = expf(-1.0f / (tauD * sr));
    float ampCoeff2 = expf(-1.0f / (tauD * 0.6f * sr));   // 2nd osc: dampened (faster) decay
    int holdSamples = (int)(hold * 0.8f * sr);
    float tauP = 0.002f + timeK * 0.35f;
    float pitchCoeff = expf(-1.0f / (tauP * sr));

    float toSine = CLAMP(0.0f, 1.0f, character * 2.0f);   // 0=triangle, >=0.5-knob=sine
    float foldMix = character > 0.5f ? (character - 0.5f) * 2.0f : 0.0f;  // fold ramp
    float foldDrive = 1.0f + foldMix * 3.0f;
    float noiseMix = grit * 0.9f;                          // keep some osc even at max
    float ratio2 = 1.0f + shape * 0.5f;                   // 2nd osc interval (unison..1.5x)

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float tv = trig[i];
      if (tv > 0.5f && I.prevTrig <= 0.5f)   // rising edge -> new hit
      {
        I.baseHz = 40.0f * powf(2.0f, pitchP * 4.5f + voct[i]);  // ~40 Hz .. ~900 Hz
        I.sweepDepth = sweepP * 24.0f;
        I.pitchCoeff = pitchCoeff;
        I.pitchEnv = 1.0f; I.ampEnv = 1.0f; I.ampEnv2 = 1.0f; I.holdLeft = holdSamples;
        I.ph1 = 0.0f; I.ph2 = 0.0f;
      }
      I.prevTrig = tv;

      float f = I.baseHz * (1.0f + I.sweepDepth * I.pitchEnv);
      I.pitchEnv *= I.pitchCoeff;

      // core waveshape: triangle -> sine -> folded triangle (Character)
      float p = I.ph1 - floorf(I.ph1);
      float tri = 4.0f * fabsf(p - 0.5f) - 1.0f;
      float sn = sineLUT(I.ph1);
      float triSine = tri + (sn - tri) * toSine;
      float w1 = triSine;
      if (foldMix > 0.0f)
      {
        float folded = sineLUT(triSine * foldDrive * 0.25f);   // wavefold re-adds harmonics
        w1 = triSine + (folded - triSine) * foldMix;
      }

      float sn2 = sineLUT(I.ph2);   // 2nd oscillator overlay (Shape)

      I.ph1 += f / sr;             I.ph1 -= floorf(I.ph1);
      I.ph2 += f * ratio2 / sr;    I.ph2 -= floorf(I.ph2);

      // amp envelopes: instant attack, Hold plateau, exp Decay (2nd osc dampened)
      if (I.holdLeft > 0) I.holdLeft--; else { I.ampEnv *= ampCoeff; }
      I.ampEnv2 *= ampCoeff2;

      float osc = w1 * I.ampEnv + shape * sn2 * I.ampEnv2;

      // Grit: blend enveloped noise in (keeps some osc), ramps toward "just noise"
      float y = osc * (1.0f - noiseMix) + noise(I.rng) * noiseMix * I.ampEnv;

      out[i] = y * level;
    }
  }

} // namespace stolmine
