#include "Tessera.h"
#include "DrumVoiceSineLUT.h"   // static half-sine LUT (no runtime trig on am335x)
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

// no-tree-vectorize is load-bearing on am335x (feedback_disable_tree_vectorize_am335x).
#pragma GCC optimize("no-tree-vectorize")

namespace stolmine
{

  // Additive MODAL engine (2026-07-19): a bank of decaying sinusoids, each mode with
  // its OWN frequency / amplitude / decay rate. This is the only way to get the
  // hardware's frequency-dependent (modal) decay - high partials ring shorter than
  // low ones (measured tau_n ~ tau0 * harm^-alpha, two-regime: gentle for the low
  // "tone" modes, steep for the high "attack" modes). See findings-dynamics.md.
  static const int NM = 12;

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
    float phase[NM];   // per-mode phase
    float env[NM];     // per-mode amplitude envelope (its own decay)
    float ratio[NM];   // per-mode frequency ratio (latched at trigger)
    float pitchEnv = 0;
    float noiseEnv = 0;
    float noiseLp = 0;   // band-limits the grit noise (measured: not white)
    int holdLeft = 0;
    float prevTrig = 0;
    float baseHz = 110, sweepDepth = 0, pitchCoeff = 0.999f;
    uint32_t rng = 0x51ee7u;
    Internal() { for (int m = 0; m < NM; m++) { phase[m] = 0; env[m] = 0; ratio[m] = 2 * m + 1; } }
  };

  Tessera::Tessera()
  {
    addInput(mTrigger);
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mF0);
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

  float Tessera::getEnvLevel() { return mpInternal->env[0]; }

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
    float f0 = CLAMP(8.0f, 8000.0f, mF0.value());
    float sweepP = CLAMP(0.0f, 1.0f, mSweep.value());

    float tau0 = 0.02f * powf(40.0f, decay);   // fundamental ring 20 ms .. 0.8 s
    // Grit shortens the whole bank past ~0.75 (measured 808-snare snap)
    float gritShorten = grit > 0.75f ? 1.0f - (grit - 0.75f) / 0.25f * 0.7f : 1.0f;
    tau0 *= gritShorten;
    float noiseCoeff = expf(-1.0f / (0.02f * powf(40.0f, decay) * sr));  // noise own env
    int holdSamples = (int)(hold * 0.8f * sr);
    float tauP = 0.002f + timeK * 0.35f;
    float pitchCoeff = expf(-1.0f / (tauP * sr));

    // Character = waveshape harmonic richness (V-shape: triangle harmonics at 0 ->
    // sine at 0.5 -> fold harmonics at 1). Shape boosts the higher "modal" partials.
    float rich = fabsf(character - 0.5f) * 2.0f;
    float roll = character < 0.5f ? 1.0f : 0.6f;   // gentler rolloff -> brighter (match HW)
    float highGain = (0.2f + shape * 0.7f + rich * 0.5f) * 0.5f;
    // grit noise band-limit: ~4 kHz one-pole (measured max-grit centroid ~3.6 kHz, not
    // white). Proper coefficient (1-exp) - the 2*pi*fc/sr approximation breaks at high fc.
    float noiseLpG = 1.0f - expf(-6.2832f * 4000.0f / sr);

    // per-mode envelope decay coefficients (block rate). Two-regime damping law (tuned
    // to the measured taus: the low "tone" modes ring together, high modes die fast ->
    // gives the measured brightness-decay ~2.3-2.4).
    float decayCoeff[NM];
    float amp[NM];
    for (int m = 0; m < NM; m++)
    {
      float harm = 2.0f * m + 1.0f;
      float alpha = (harm <= 7.0f) ? 0.4f : 1.2f;
      float taun = tau0 * powf(harm, -alpha);
      if (taun < 0.002f) taun = 0.002f;
      decayCoeff[m] = expf(-1.0f / (taun * sr));
      float rl = (harm <= 7.0f) ? roll : 1.4f;   // high modes roll off steeply (attack only)
      amp[m] = (m == 0) ? 1.0f : highGain / powf(harm, rl);
    }

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float tv = trig[i];
      if (tv > 0.5f && I.prevTrig <= 0.5f)   // rising edge -> new hit
      {
        I.baseHz = f0 * powf(2.0f, voct[i]);
        I.sweepDepth = sweepP * 10.0f;   // moderated (24 crushed the low-end vs HW throw)
        I.pitchCoeff = pitchCoeff;
        I.pitchEnv = 1.0f; I.noiseEnv = 1.0f; I.holdLeft = holdSamples;
        for (int m = 0; m < NM; m++)
        {
          // odd-harmonic modes, stretched (inharmonic) by Shape -> the modal spread
          I.ratio[m] = (2.0f * m + 1.0f) * (1.0f + shape * 0.05f * m);
          // start at quarter phase (cosine) so all modes contribute energy at t=0 -
          // an instant broadband impact (punch), not a fade-in from sin(0)=0.
          I.phase[m] = 0.25f;
          I.env[m] = amp[m];
        }
      }
      I.prevTrig = tv;

      I.pitchEnv *= I.pitchCoeff;
      I.noiseEnv *= noiseCoeff;
      float sweepMul = 1.0f + I.sweepDepth * I.pitchEnv;

      float y = 0.0f;
      bool held = I.holdLeft > 0;
      for (int m = 0; m < NM; m++)
      {
        float fm = I.baseHz * I.ratio[m] * sweepMul;
        I.phase[m] += fm / sr; I.phase[m] -= floorf(I.phase[m]);
        if (!held) I.env[m] *= decayCoeff[m];
        y += I.env[m] * sineLUT(I.phase[m]);
      }
      if (held) I.holdLeft--;

      // Grit noise: band-limited (one-pole LP) with its own env (post, keeps some tone)
      float noiseMix = grit * 0.9f;
      I.noiseLp += noiseLpG * (noise(I.rng) - I.noiseLp);
      y = y * (1.0f - noiseMix * 0.5f) + I.noiseLp * noiseMix * I.noiseEnv;

      out[i] = y * level;
    }
  }

} // namespace stolmine
