// Helicase -- 2-operator FM oscillator for ER-301
// OPL3-inspired architecture: carrier + modulator with feedback and discontinuity folder

#include "Helicase.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static const float kTwoPi = 6.28318530718f;

  // OPL3 waveform set (8 shapes)
  // Used for modulator shape (hard-switched) and discontinuity folder (morphable)
  static inline float opl3Wave(float phase, int shape)
  {
    float s = sinf(phase * kTwoPi);
    // Wrap phase to 0-1
    float p = phase - floorf(phase);
    switch (shape)
    {
    case 0: return s;                                             // sine
    case 1: return s > 0.0f ? s : 0.0f;                          // half-sine
    case 2: return fabsf(s);                                      // abs-sine
    case 3: return (p < 0.25f || p > 0.75f) ? fabsf(s) : 0.0f;  // quarter-sine
    case 4: return ((int)(p * 2.0f) & 1) ? 0.0f : s;             // alternating
    case 5: return ((int)(p * 2.0f) & 1) ? 0.0f : fabsf(s);      // camel
    case 6: return s >= 0.0f ? 1.0f : -1.0f;                     // square
    case 7: return 1.0f - p * 2.0f;                               // log saw
    }
    return s;
  }

  // Discontinuity folder: morph between two adjacent OPL3 transfer functions
  static inline float discFold(float input, float typeF)
  {
    int t0 = (int)typeF;
    int t1 = t0 + 1;
    if (t0 < 0) t0 = 0;
    if (t1 > 7) t1 = 7;
    if (t0 > 7) t0 = 7;
    float frac = typeF - (float)t0;

    // Apply each transfer function to the input
    // Treat input as a phase-like value: map -1..1 to 0..1
    float p = (input + 1.0f) * 0.5f;
    float w0 = opl3Wave(p, t0);
    float w1 = opl3Wave(p, t1);
    return w0 + (w1 - w0) * frac;
  }

  struct Helicase::Internal
  {
    float carrierPhase;
    float modPhase;
    float modFeedbackState;
    bool syncWasHigh;

    // Ring buffers for viz
    float outputRing[256];
    float modRing[256];
    int ringPos;

    // Current state for curve viz
    float curCarrierPhase;
    float curCarrierOutput;

    // Decimation counter for ring buffer (capture full attractor shape)
    int ringDecimCounter;
    int ringDecimRate;

    void Init()
    {
      carrierPhase = 0.0f;
      modPhase = 0.0f;
      modFeedbackState = 0.0f;
      syncWasHigh = false;
      memset(outputRing, 0, sizeof(outputRing));
      memset(modRing, 0, sizeof(modRing));
      ringPos = 0;
      ringDecimCounter = 0;
      ringDecimRate = 8;
      curCarrierPhase = 0.0f;
      curCarrierOutput = 0.0f;
    }
  };

  Helicase::Helicase()
  {
    addInput(mVOct);
    addInput(mSync);
    addOutput(mOut);
    addParameter(mFundamental);
    addParameter(mModMix);
    addParameter(mModIndex);
    addParameter(mDiscIndex);
    addParameter(mDiscType);
    addParameter(mRatio);
    addParameter(mFeedback);
    addParameter(mModShape);
    addParameter(mFine);
    addParameter(mLevel);
    addParameter(mCarrierShape);
    addParameter(mLinExpo);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Helicase::~Helicase()
  {
    delete mpInternal;
  }

  float Helicase::getOutputSample(int idx)
  {
    if (idx < 0 || idx > 255) return 0.0f;
    return mpInternal->outputRing[(mpInternal->ringPos + idx) & 255];
  }

  float Helicase::getModulatorSample(int idx)
  {
    if (idx < 0 || idx > 255) return 0.0f;
    return mpInternal->modRing[(mpInternal->ringPos + idx) & 255];
  }

  float Helicase::getCarrierPhase()
  {
    return mpInternal->curCarrierPhase;
  }

  float Helicase::getCarrierOutput()
  {
    return mpInternal->curCarrierOutput;
  }

  float Helicase::getDiscIndex()
  {
    return CLAMP(0.0f, 1.0f, mDiscIndex.value());
  }

  float Helicase::getDiscType()
  {
    return CLAMP(0.0f, 7.0f, mDiscType.value());
  }

  void Helicase::process()
  {
    Internal &s = *mpInternal;
    float *voct = mVOct.buffer();
    float *sync = mSync.buffer();
    float *out = mOut.buffer();

    float sr = globalConfig.sampleRate;
    float f0 = CLAMP(0.1f, sr * 0.49f, mFundamental.value());
    float modMix = CLAMP(0.0f, 1.0f, mModMix.value());
    float modIndex = CLAMP(0.0f, 10.0f, mModIndex.value());
    float discIndex = CLAMP(0.0f, 1.0f, mDiscIndex.value());
    float discTypeF = CLAMP(0.0f, 7.0f, mDiscType.value());
    float ratio = CLAMP(0.5f, 16.0f, mRatio.value());
    float feedback = CLAMP(0.0f, 1.0f, mFeedback.value());
    int modShape = CLAMP(0, 7, (int)(mModShape.value() + 0.5f));
    float fine = CLAMP(-100.0f, 100.0f, mFine.value());
    float level = CLAMP(0.0f, 1.0f, mLevel.value());
    int carrierShape = CLAMP(0, 7, (int)(mCarrierShape.value() + 0.5f));
    bool linFM = mLinExpo.value() > 0.5f;

    // V/Oct (block-rate)
    // V/Oct: Lua applies 10x ConstantGain (FULLSCALE_IN_VOLTS = 10),
    // so voct arrives as 1.0 per octave. No additional scaling needed.
    float pitch = voct[0];
    float carrierFreq = f0 * powf(2.0f, pitch + fine / 1200.0f);
    if (carrierFreq > sr * 0.49f) carrierFreq = sr * 0.49f;
    float modFreq = carrierFreq * ratio;

    float carrierInc = carrierFreq / sr;
    float modInc = modFreq / sr;

    // Adapt ring buffer decimation so 256 entries cover ~8 carrier cycles
    float samplesPerCycle = sr / (carrierFreq > 0.1f ? carrierFreq : 0.1f);
    int targetDecim = (int)(samplesPerCycle * 8.0f / 256.0f);
    if (targetDecim < 1) targetDecim = 1;
    if (targetDecim > 64) targetDecim = 64;
    s.ringDecimRate = targetDecim;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Sync: reset carrier phase on rising edge
      bool syncHigh = sync[i] > 0.5f;
      if (syncHigh && !s.syncWasHigh)
        s.carrierPhase = 0.0f;
      s.syncWasHigh = syncHigh;

      // Modulator
      s.modPhase += modInc;
      s.modPhase -= floorf(s.modPhase);

      // Self-feedback: modulator output feeds back into its own phase
      float modPhaseFB = s.modPhase + feedback * s.modFeedbackState * 0.5f;
      float modOut = opl3Wave(modPhaseFB, modShape);
      s.modFeedbackState = modOut;

      // FM: modulator modulates carrier phase increment
      // Standard FM: phase += (fc + modOut * fm * index) / sr
      // modIndex scales the frequency deviation by the modulator frequency
      float fmAmount = modOut * modIndex * modInc;

      s.carrierPhase += carrierInc + fmAmount;
      s.carrierPhase -= floorf(s.carrierPhase);

      // Carrier (always FM'd)
      float carrSig = opl3Wave(s.carrierPhase, carrierShape);

      // Discontinuity folder
      float folded = carrSig;
      if (discIndex > 0.001f)
      {
        float f = discFold(carrSig, discTypeF);
        folded = carrSig * (1.0f - discIndex) + f * discIndex;
      }

      // Mix: carrier always present, modulator added on top
      float mixed = folded + modOut * modMix;
      float finalOut = mixed * level;

      // Clamp output
      if (finalOut > 1.5f) finalOut = 1.5f;
      if (finalOut < -1.5f) finalOut = -1.5f;

      out[i] = finalOut;

      // Ring buffers for viz -- decimated to capture full attractor shape
      s.ringDecimCounter++;
      if (s.ringDecimCounter >= s.ringDecimRate)
      {
        s.ringDecimCounter = 0;
        s.outputRing[s.ringPos] = finalOut;
        s.modRing[s.ringPos] = modOut;
        s.ringPos = (s.ringPos + 1) & 255;
      }
    }

    // Store current state for curve viz (last sample)
    s.curCarrierPhase = s.carrierPhase;
    s.curCarrierOutput = out[FRAMELENGTH - 1];
  }

} // namespace stolmine
