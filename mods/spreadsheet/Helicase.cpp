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

  // Fold shapes 8-15: actual wavefolding transfer functions
  // Input is -1..1 (raw carrier signal, not phase-mapped)
  static inline float foldShape(float x, int shape)
  {
    switch (shape)
    {
    case 8: // triangle fold -- Serge-style, bounces off rails
    {
      float y = fmodf(fabsf(x + 1.0f), 4.0f);
      return (y < 2.0f) ? y - 1.0f : 3.0f - y;
    }
    case 9: // sine fold -- Buchla-style, wraps through sine
      return sinf(x * (float)M_PI);
    case 10: // hard fold -- sharp V reflect at boundaries
    {
      float y = fmodf(fabsf(x + 1.0f), 2.0f);
      return y < 1.0f ? y * 2.0f - 1.0f : 1.0f - (y - 1.0f) * 2.0f;
    }
    case 11: // staircase -- quantized levels
    {
      float steps = 6.0f;
      return floorf(x * steps + 0.5f) / steps;
    }
    case 12: // wrap -- phase wrapping, input exceeding +-1 wraps around
    {
      float y = fmodf(x + 1.0f, 2.0f);
      if (y < 0.0f) y += 2.0f;
      return y - 1.0f;
    }
    case 13: // asymmetric fold -- different up/down slopes
    {
      float y = fmodf(fabsf(x * 1.5f + 1.0f), 4.0f);
      return (y < 2.0f) ? y - 1.0f : 3.0f - y;
    }
    case 14: // chebyshev T3 -- 3rd order harmonic generation
    {
      float x2 = x * x;
      return x * (4.0f * x2 - 3.0f);
    }
    case 15: // ring fold -- abs with DC cycling
      return fabsf(sinf(x * (float)M_PI * 2.0f)) * 2.0f - 1.0f;
    }
    return x;
  }

  // Discontinuity folder: morph between adjacent transfer functions (0-15)
  // 0-7: OPL3 operations (phase-mapped), 8-15: wavefolders (direct)
  static inline float discFold(float input, float typeF)
  {
    int t0 = (int)typeF;
    int t1 = t0 + 1;
    if (t0 < 0) t0 = 0;
    if (t1 > 15) t1 = 15;
    if (t0 > 15) t0 = 15;
    float frac = typeF - (float)t0;

    auto evalShape = [](float inp, int t) -> float {
      if (t <= 7)
      {
        float p = (inp + 1.0f) * 0.5f;
        return opl3Wave(p, t);
      }
      return foldShape(inp, t);
    };

    float w0 = evalShape(input, t0);
    float w1 = evalShape(input, t1);
    return w0 + (w1 - w0) * frac;
  }

  struct Helicase::Internal
  {
    float carrierPhase;
    float modPhase;
    float modFeedbackState;
    bool syncWasHigh;
    bool syncPending;

    // Ring buffers for viz
    float outputRing[256];
    float modRing[256];
    int ringPos;

    // Current state for curve viz
    float curCarrierPhase;
    float curCarrierOutput;

    // Slewed frequency for smooth lin FM pitch tracking
    float slewCarrierFreq;
    float slewModFreq;

    // Decimation counter for ring buffer (capture full attractor shape)
    int ringDecimCounter;
    int ringDecimRate;

    void Init()
    {
      carrierPhase = 0.0f;
      modPhase = 0.0f;
      modFeedbackState = 0.0f;
      syncWasHigh = false;
      syncPending = false;
      memset(outputRing, 0, sizeof(outputRing));
      memset(modRing, 0, sizeof(modRing));
      ringPos = 0;
      ringDecimCounter = 0;
      ringDecimRate = 8;
      curCarrierPhase = 0.0f;
      curCarrierOutput = 0.0f;
      slewCarrierFreq = 110.0f;
      slewModFreq = 220.0f;
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
    addParameter(mSyncThreshold);
    addOption(mLinExpo);

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

  bool Helicase::isLinFM()
  {
    return mLinExpo.value() == 1;
  }

  void Helicase::toggleLinFM()
  {
    mLinExpo.set(isLinFM() ? 2 : 1);
  }

  float Helicase::getDiscType()
  {
    return CLAMP(0.0f, 15.0f, mDiscType.value());
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
    float discTypeF = CLAMP(0.0f, 15.0f, mDiscType.value());
    float ratio = CLAMP(0.5f, 16.0f, mRatio.value());
    float feedback = CLAMP(0.0f, 1.0f, mFeedback.value());
    int modShape = CLAMP(0, 7, (int)(mModShape.value() + 0.5f));
    float fine = CLAMP(-100.0f, 100.0f, mFine.value());
    float level = CLAMP(0.0f, 1.0f, mLevel.value());
    int carrierShape = CLAMP(0, 7, (int)(mCarrierShape.value() + 0.5f));
    float syncThreshold = CLAMP(0.0f, 1.0f, mSyncThreshold.value());
    bool linFM = mLinExpo.value() == 1;

    // V/Oct: Lua applies 10x ConstantGain, arrives as 1.0 per octave
    float pitch = voct[0];
    float targetCarrierFreq = f0 * powf(2.0f, pitch + fine / 1200.0f);
    if (targetCarrierFreq > sr * 0.49f) targetCarrierFreq = sr * 0.49f;
    float targetModFreq = targetCarrierFreq * ratio;

    // For expo FM, use block-rate frequency directly
    // For lin FM, slew per-sample for clean pitch tracking
    float carrierFreq, modFreq;
    if (!linFM)
    {
      carrierFreq = targetCarrierFreq;
      modFreq = targetModFreq;
      s.slewCarrierFreq = carrierFreq;
      s.slewModFreq = modFreq;
    }

    // Adapt ring buffer decimation
    float samplesPerCycle = sr / (targetCarrierFreq > 0.1f ? targetCarrierFreq : 0.1f);
    int targetDecim = (int)(samplesPerCycle * 8.0f / 256.0f);
    if (targetDecim < 1) targetDecim = 1;
    if (targetDecim > 64) targetDecim = 64;
    s.ringDecimRate = targetDecim;

    // Linear ramp: constant step per sample to reach target by end of block
    float carrierStep = (targetCarrierFreq - s.slewCarrierFreq) / (float)FRAMELENGTH;
    float modStep = (targetModFreq - s.slewModFreq) / (float)FRAMELENGTH;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      if (linFM)
      {
        s.slewCarrierFreq += carrierStep;
        s.slewModFreq += modStep;
        carrierFreq = s.slewCarrierFreq;
        modFreq = s.slewModFreq;
      }
      float carrierInc = carrierFreq / sr;
      float modInc = modFreq / sr;
      // Sync: phase-receptivity check (JF-inspired)
      // Latch rising edge as pending, fire when modulator reaches threshold
      bool syncHigh = sync[i] > 0.5f;
      if (syncHigh && !s.syncWasHigh)
        s.syncPending = true;
      s.syncWasHigh = syncHigh;

      float prevModPhase = s.modPhase;

      // Modulator
      s.modPhase += modInc;
      s.modPhase -= floorf(s.modPhase);

      // Phase-receptivity: fire pending sync when modulator crosses threshold
      if (s.syncPending)
      {
        // Clear pending if modulator wrapped (missed this cycle)
        if (s.modPhase < prevModPhase)
          s.syncPending = false;

        if (s.modPhase >= syncThreshold)
        {
          s.carrierPhase = 0.0f;
          s.syncPending = false;
        }
      }

      // Self-feedback: modulator output feeds back into its own phase
      float fb = tanhf(s.modFeedbackState) * feedback * 0.5f;
      float modPhaseFB = s.modPhase + fb;
      float modOut = opl3Wave(modPhaseFB, modShape);
      s.modFeedbackState = modOut;

      // FM: modulator modulates carrier phase increment
      float fmAmount;
      if (linFM)
        // Linear through-zero FM: deviation in Hz = modIndex * 100Hz
        // Independent of carrier frequency, allows negative frequencies (TZFM)
        fmAmount = modOut * modIndex * 100.0f / sr;
      else
        // Exponential FM: ratio-preserving, deviation scales with modulator freq
        fmAmount = modOut * modIndex * modInc;

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
