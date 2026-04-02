// MultitapDelay -- Rainmaker-inspired multitap delay for ER-301

#include "MultitapDelay.h"
#include <od/config.h>
#include <od/extras/BigHeap.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>
#include <new>

#ifndef TEST
#define TEST
#endif
#include "stmlib/dsp/filter.h"

namespace stolmine
{

  struct MultitapDelay::Internal
  {
    // Delay buffer (shared circular buffer, BigHeap allocated)
    char *buffer = 0;
    int writeIndex = 0;

    // Per-tap data
    float tapTime[kMaxTaps];   // 0-1 within master window
    float tapLevel[kMaxTaps];  // 0-1
    float tapPan[kMaxTaps];    // -1 to +1

    // Per-tap filter
    float filterCutoff[kMaxTaps]; // 0-1 normalized
    float filterQ[kMaxTaps];      // 0-1
    int filterType[kMaxTaps];
    stmlib::Svf filters[kMaxTaps];
    float cachedBandQ[kMaxTaps]; // for feedback compensation

    // Per-tap pitch (octaves, -2 to +2)
    float tapPitch[kMaxTaps];

    // Per-tap energy (for visualization)
    float tapEnergy[kMaxTaps];

    // Granular pitch shift state
    struct TapGrain
    {
      float phase;    // 0-1 through envelope
      float readPos;  // fractional sample position in buffer
      float phaseDelta;
      float speed;
      bool active;
    };
    TapGrain grains[kMaxTaps][kGrainsPerTap];
    int grainSpawnCounter[kMaxTaps];

    // Sine LUT for grain envelope (avoid sinf in inner loop)
    float sineLUT[kSineLUTSize];

    // Feedback damping (one-pole filters on feedback path)
    float fbFilterState = 0.0f;
    float fbHpState = 0.0f;

    float lookupSine(float phase)
    {
      float idx = phase * (float)(kSineLUTSize - 1);
      int i0 = (int)idx;
      if (i0 >= kSineLUTSize - 1) return sineLUT[kSineLUTSize - 1];
      float frac = idx - (float)i0;
      return sineLUT[i0] + (sineLUT[i0 + 1] - sineLUT[i0]) * frac;
    }

    void Init()
    {
      writeIndex = 0;
      for (int i = 0; i < kMaxTaps; i++)
      {
        tapTime[i] = (float)(i + 1) / (float)kMaxTaps;
        tapLevel[i] = 1.0f;
        tapPan[i] = 0.0f;
        tapPitch[i] = 0.0f;
        filterCutoff[i] = 10000.0f; // Hz
        filterQ[i] = 0.0f;
        filterType[i] = TAP_FILTER_OFF;
        filters[i].Init();
        tapEnergy[i] = 0.0f;
        grainSpawnCounter[i] = 0;
        for (int g = 0; g < kGrainsPerTap; g++)
          grains[i][g].active = false;
      }
      // Pre-compute sine LUT (half sine = grain envelope)
      for (int i = 0; i < kSineLUTSize; i++)
      {
        float t = (float)i / (float)(kSineLUTSize - 1);
        sineLUT[i] = sinf(t * 3.14159265f);
      }
    }
  };

  MultitapDelay::MultitapDelay()
  {
    addInput(mIn);
    addOutput(mOut);
    addOutput(mOutR);
    addParameter(mMasterTime);
    addParameter(mFeedback);
    addParameter(mFeedbackTone);
    addParameter(mMix);
    addParameter(mTapCount);
    addParameter(mVOctPitch);
    addParameter(mSkew);
    addParameter(mGrainSize);
    addParameter(mEditTapPitch);
    addParameter(mInputLevel);
    addParameter(mOutputLevel);
    addParameter(mTanhAmt);
    addParameter(mEditTapTime);
    addParameter(mEditTapLevel);
    addParameter(mEditTapPan);
    addParameter(mEditFilterCutoff);
    addParameter(mEditFilterQ);
    addParameter(mEditFilterType);

    mpInternal = new Internal();
    mpInternal->Init();
    memset(mCachedDelaySamples, 0, sizeof(mCachedDelaySamples));
    // Init pan cache to center
    for (int i = 0; i < kMaxTaps; i++)
    {
      mCachedPanL[i] = 0.707f;
      mCachedPanR[i] = 0.707f;
    }
  }

  MultitapDelay::~MultitapDelay()
  {
    deallocate();
    delete mpInternal;
  }

  // --- Buffer allocation ---

  bool MultitapDelay::allocate(int Ns)
  {
    deallocate();
    int nbytes = Ns * sizeof(float);
    mpInternal->buffer = new (std::nothrow) char[nbytes];
    if (mpInternal->buffer)
      memset(mpInternal->buffer, 0, nbytes);
    return mpInternal->buffer != 0;
  }

  void MultitapDelay::deallocate()
  {
    if (mpInternal->buffer)
    {
      delete[] mpInternal->buffer;
      mpInternal->buffer = 0;
    }
  }

  float MultitapDelay::allocateTimeUpTo(float seconds)
  {
    int Ns = globalConfig.sampleRate * MAX(0.001f, seconds);
    int Nf = (Ns / FRAMELENGTH + 1);
    Ns = Nf * FRAMELENGTH;

    if (Ns == mMaxDelayInSamples)
      return Ns * globalConfig.samplePeriod;

    mMaxDelayInSamples = 0;
    mpInternal->writeIndex = 0;

    while (Nf > 1)
    {
      if (allocate(Nf * FRAMELENGTH))
      {
        mMaxDelayInSamples = Nf * FRAMELENGTH;
        return mMaxDelayInSamples * globalConfig.samplePeriod;
      }
      Nf /= 2;
    }

    return 0;
  }

  float MultitapDelay::maximumDelayTime()
  {
    return mMaxDelayInSamples * globalConfig.samplePeriod;
  }

  // --- Tap accessors ---

  float MultitapDelay::getTapTime(int i) { return mpInternal->tapTime[CLAMP(0, kMaxTaps - 1, i)]; }
  void MultitapDelay::setTapTime(int i, float v) { mpInternal->tapTime[CLAMP(0, kMaxTaps - 1, i)] = CLAMP(0.0f, 1.0f, v); }
  float MultitapDelay::getTapLevel(int i) { return mpInternal->tapLevel[CLAMP(0, kMaxTaps - 1, i)]; }
  void MultitapDelay::setTapLevel(int i, float v) { mpInternal->tapLevel[CLAMP(0, kMaxTaps - 1, i)] = CLAMP(0.0f, 1.0f, v); }
  float MultitapDelay::getTapPan(int i) { return mpInternal->tapPan[CLAMP(0, kMaxTaps - 1, i)]; }
  void MultitapDelay::setTapPan(int i, float v) { mpInternal->tapPan[CLAMP(0, kMaxTaps - 1, i)] = CLAMP(-1.0f, 1.0f, v); }
  float MultitapDelay::getTapPitch(int i) { return mpInternal->tapPitch[CLAMP(0, kMaxTaps - 1, i)]; }
  void MultitapDelay::setTapPitch(int i, float v) { mpInternal->tapPitch[CLAMP(0, kMaxTaps - 1, i)] = CLAMP(-24.0f, 24.0f, v); }

  // --- Filter accessors ---

  float MultitapDelay::getFilterCutoff(int i) { return mpInternal->filterCutoff[CLAMP(0, kMaxTaps - 1, i)]; }
  void MultitapDelay::setFilterCutoff(int i, float v) { mpInternal->filterCutoff[CLAMP(0, kMaxTaps - 1, i)] = CLAMP(20.0f, 10000.0f, v); }
  float MultitapDelay::getFilterQ(int i) { return mpInternal->filterQ[CLAMP(0, kMaxTaps - 1, i)]; }
  void MultitapDelay::setFilterQ(int i, float v) { mpInternal->filterQ[CLAMP(0, kMaxTaps - 1, i)] = CLAMP(0.0f, 1.0f, v); }
  int MultitapDelay::getFilterType(int i) { return mpInternal->filterType[CLAMP(0, kMaxTaps - 1, i)]; }
  void MultitapDelay::setFilterType(int i, int v) { mpInternal->filterType[CLAMP(0, kMaxTaps - 1, i)] = CLAMP(0, TAP_FILTER_COUNT - 1, v); }

  // --- Edit buffer ---

  void MultitapDelay::loadTap(int i)
  {
    i = CLAMP(0, kMaxTaps - 1, i);
    mLastLoadedTap = i;
    mEditTapTime.hardSet(mpInternal->tapTime[i]);
    mEditTapLevel.hardSet(mpInternal->tapLevel[i]);
    mEditTapPan.hardSet(mpInternal->tapPan[i]);
    mEditTapPitch.hardSet(mpInternal->tapPitch[i]);
  }

  void MultitapDelay::storeTap(int i)
  {
    i = CLAMP(0, kMaxTaps - 1, i);
    mpInternal->tapTime[i] = CLAMP(0.0f, 1.0f, mEditTapTime.value());
    mpInternal->tapLevel[i] = CLAMP(0.0f, 1.0f, mEditTapLevel.value());
    float pan = CLAMP(-1.0f, 1.0f, mEditTapPan.value());
    mpInternal->tapPan[i] = pan;
    // Update cached pan
    float a = (pan + 1.0f) * 0.25f * 3.14159f;
    mCachedPanL[i] = cosf(a);
    mCachedPanR[i] = sinf(a);
    mpInternal->tapPitch[i] = CLAMP(-24.0f, 24.0f, floorf(mEditTapPitch.value() + 0.5f));
  }

  void MultitapDelay::loadFilter(int i)
  {
    i = CLAMP(0, kMaxTaps - 1, i);
    mLastLoadedFilter = i;
    mEditFilterCutoff.hardSet(mpInternal->filterCutoff[i]);
    mEditFilterQ.hardSet(mpInternal->filterQ[i]);
    mEditFilterType.hardSet((float)mpInternal->filterType[i]);
  }

  void MultitapDelay::storeFilter(int i)
  {
    i = CLAMP(0, kMaxTaps - 1, i);
    mpInternal->filterCutoff[i] = CLAMP(20.0f, 10000.0f, mEditFilterCutoff.value());
    mpInternal->filterQ[i] = CLAMP(0.0f, 1.0f, mEditFilterQ.value());
    mpInternal->filterType[i] = CLAMP(0, TAP_FILTER_COUNT - 1, (int)(mEditFilterType.value() + 0.5f));
  }

  int MultitapDelay::getTapCount()
  {
    mCachedTapCount = CLAMP(1, kMaxTaps, (int)(mTapCount.value() + 0.5f));
    return mCachedTapCount;
  }

  float MultitapDelay::getTapEnergy(int i)
  {
    return sqrtf(mpInternal->tapEnergy[CLAMP(0, kMaxTaps - 1, i)]);
  }

  // --- Process ---

  void MultitapDelay::process()
  {
    Internal &s = *mpInternal;

    if (!s.buffer || mMaxDelayInSamples == 0)
    {
      // No buffer allocated -- passthrough
      float *in = mIn.buffer();
      float *out = mOut.buffer();
      float *outR = mOutR.buffer();
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        out[i] = in[i];
        outR[i] = in[i];
      }
      return;
    }

    float *in = mIn.buffer();
    float *out = mOut.buffer();
    float *outR = mOutR.buffer();
    float *buf = (float *)s.buffer;

    int tapCount = CLAMP(1, kMaxTaps, (int)(mTapCount.value() + 0.5f));
    mCachedTapCount = tapCount;

    float masterTime = CLAMP(0.001f, 2.0f, mMasterTime.value());
    float feedback = CLAMP(0.0f, 0.95f, mFeedback.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float inputLevel = CLAMP(0.0f, 4.0f, mInputLevel.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float tanhAmt = CLAMP(0.0f, 1.0f, mTanhAmt.value());
    float skew = mSkew.value();
    float grainSizeParam = CLAMP(0.0f, 1.0f, mGrainSize.value());

    int maxDelay = mMaxDelayInSamples;
    float sr = globalConfig.sampleRate;

    // V/Oct master pitch (ConstantOffset outputs 0.1 per octave in 10Vpp range)
    float voctPitch = mVOctPitch.value() * 10.0f; // scale to octaves

    // Grain size: 0=5ms, 0.5=30ms, 1.0=100ms
    int grainDuration = (int)(sr * (0.005f + grainSizeParam * 0.095f));
    if (grainDuration < 64) grainDuration = 64;
    int grainPeriod = (int)(grainDuration * 0.75f);
    if (grainPeriod < 32) grainPeriod = 32;
    float grainPhaseDelta = 1.0f / (float)grainDuration;

    // Recompute tap distribution only when params change
    bool distDirty = (tapCount != mLastTapCount || skew != mLastSkew || masterTime != mLastMasterTime);
    if (distDirty)
    {
      float skewExp = powf(2.0f, skew);
      for (int t = 0; t < tapCount; t++)
      {
        float pos = powf((float)(t + 1) / (float)tapCount, skewExp);
        s.tapTime[t] = pos;
        mCachedDelaySamples[t] = pos * masterTime * sr;
      }
      // Pre-cache pan coefficients (equal power)
      for (int t = 0; t < tapCount; t++)
      {
        float pan = s.tapPan[t];
        float a = (pan + 1.0f) * 0.25f * 3.14159f;
        mCachedPanL[t] = cosf(a);
        mCachedPanR[t] = sinf(a);
      }
      mLastTapCount = tapCount;
      mLastSkew = skew;
      mLastMasterTime = masterTime;
      loadTap(mLastLoadedTap);
    }

    // Update filter coefficients -- FFB parametrization
    for (int t = 0; t < tapCount; t++)
    {
      // filterCutoff stored as Hz (20-20000), convert to normalized
      float cutoffHz = CLAMP(20.0f, 10000.0f, s.filterCutoff[t]);
      float freq = cutoffHz / sr;
      freq = CLAMP(0.0001f, 0.49f, freq);
      float q = 1.0f + 29.0f * s.filterQ[t] * s.filterQ[t]; // 1 to 30, moderate resonance
      float bandQ = q * (0.5f + freq * 2.0f);
      if (bandQ < 0.5f) bandQ = 0.5f;
      s.cachedBandQ[t] = bandQ;
      s.filters[t].set_f_q<stmlib::FREQUENCY_FAST>(freq, bandQ);
    }

    float fbNorm = feedback / (1.0f + sqrtf((float)tapCount));

    // Pre-compute per-tap grain speeds (avoid powf in inner loop)
    float tapSpeeds[kMaxTaps];
    for (int t = 0; t < tapCount; t++)
      tapSpeeds[t] = powf(2.0f, voctPitch + s.tapPitch[t] / 12.0f);

    // Feedback tone: -1 = dark (LP), 0 = flat, +1 = bright (HP)
    float tone = CLAMP(-1.0f, 1.0f, mFeedbackTone.value());
    float fbFilterCoeff;
    if (tone <= 0.0f)
    {
      // LP: coefficient from 0.05 (very dark, ~400Hz) to 1.0 (flat)
      fbFilterCoeff = 0.05f + 0.95f * (1.0f + tone);
    }
    else
    {
      // HP: coefficient = tone controls how much low end is removed
      fbFilterCoeff = 1.0f; // LP stays open, HP applied separately
    }

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i] * inputLevel;

      // Write input + feedback to buffer
      if (s.writeIndex >= maxDelay) s.writeIndex = 0;
      buf[s.writeIndex] = x;

      float wetL = 0.0f;
      float wetR = 0.0f;
      float lastTapOut = 0.0f;

      for (int t = 0; t < tapCount; t++)
      {
        if (s.tapLevel[t] < 0.001f)
          continue;

        float delaySamples = mCachedDelaySamples[t];
        float tapSpeed = tapSpeeds[t];

        // Spawn new grain if counter expired
        s.grainSpawnCounter[t]--;
        if (s.grainSpawnCounter[t] <= 0)
        {
          // Find an inactive grain slot
          for (int g = 0; g < kGrainsPerTap; g++)
          {
            if (!s.grains[t][g].active)
            {
              s.grains[t][g].active = true;
              s.grains[t][g].phase = 0.0f;
              s.grains[t][g].phaseDelta = grainPhaseDelta;
              s.grains[t][g].speed = tapSpeed;
              // Start reading from the tap's delay position
              float startPos = (float)s.writeIndex - delaySamples;
              while (startPos < 0.0f) startPos += (float)maxDelay;
              s.grains[t][g].readPos = startPos;
              break;
            }
          }
          s.grainSpawnCounter[t] = grainPeriod;
        }

        // Sum active grains for this tap
        float tapOut = 0.0f;
        for (int g = 0; g < kGrainsPerTap; g++)
        {
          Internal::TapGrain &gr = s.grains[t][g];
          if (!gr.active) continue;
          // Sine envelope from LUT
          float env = s.lookupSine(gr.phase);
          // Read from buffer with linear interpolation
          int idx = ((int)gr.readPos) % maxDelay;
          if (idx < 0) idx += maxDelay;
          float frac = gr.readPos - floorf(gr.readPos);
          int idx2 = (idx + 1) % maxDelay;
          float sample = buf[idx] + (buf[idx2] - buf[idx]) * frac;
          tapOut += sample * env;
          // Advance grain
          gr.readPos += gr.speed;
          gr.phase += gr.phaseDelta;
          if (gr.phase >= 1.0f) gr.active = false;
        }

        // Apply per-tap filter
        switch (s.filterType[t])
        {
        case TAP_FILTER_OFF:
          break; // bypass
        case TAP_FILTER_LP:
          tapOut = s.filters[t].Process<stmlib::FILTER_MODE_LOW_PASS>(tapOut);
          break;
        case TAP_FILTER_BP:
          tapOut = s.filters[t].Process<stmlib::FILTER_MODE_BAND_PASS>(tapOut);
          break;
        case TAP_FILTER_HP:
          tapOut = s.filters[t].Process<stmlib::FILTER_MODE_HIGH_PASS>(tapOut);
          break;
        case TAP_FILTER_NOTCH:
        {
          float lp, hp;
          s.filters[t].Process<stmlib::FILTER_MODE_LOW_PASS, stmlib::FILTER_MODE_HIGH_PASS>(tapOut, &lp, &hp);
          tapOut = lp + hp;
          break;
        }
        default:
          break;
        }

        float filteredOut = tapOut * s.tapLevel[t];

        // Energy follower for visualization
        float e = filteredOut * filteredOut;
        s.tapEnergy[t] += (e - s.tapEnergy[t]) * 0.001f;

        // Pan: pre-cached equal power (full resonant signal to output)
        wetL += filteredOut * mCachedPanL[t];
        wetR += filteredOut * mCachedPanR[t];

        // Feedback gets Q-compensated signal (resonance audible but doesn't accumulate)
        lastTapOut = filteredOut / (1.0f + s.cachedBandQ[t] * 0.1f);
      }

      // Feedback: tone-controlled damping + soft limiter
      float fb = lastTapOut * fbNorm;
      s.fbFilterState += (fb - s.fbFilterState) * fbFilterCoeff;
      float fbOut = s.fbFilterState;
      if (tone > 0.0f)
      {
        // HP: subtract lowpassed version (more tone = more bass removed)
        float lpCoeff = 1.0f - tone * 0.95f; // 1.0 (flat) down to 0.05 (heavy HP)
        s.fbHpState += (fb - s.fbHpState) * lpCoeff;
        fbOut = fb - s.fbHpState * tone;
      }
      buf[s.writeIndex] += tanhf(fbOut);

      // Advance write index
      s.writeIndex = (s.writeIndex + 1) % maxDelay;

      // Mix
      float mixedL = x * (1.0f - mix) + wetL * mix;
      float mixedR = x * (1.0f - mix) + wetR * mix;

      // User saturation
      if (tanhAmt > 0.001f)
      {
        float drive = 1.0f + tanhAmt * 3.0f;
        mixedL = mixedL * (1.0f - tanhAmt) + tanhf(mixedL * drive) * tanhAmt;
        mixedR = mixedR * (1.0f - tanhAmt) + tanhf(mixedR * drive) * tanhAmt;
      }

      // Output limiter (invisible, always on)
      out[i] = tanhf(mixedL * outputLevel);
      outR[i] = tanhf(mixedR * outputLevel);
    }
  }

} // namespace stolmine
