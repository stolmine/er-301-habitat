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

    // Per-tap energy (for visualization)
    float tapEnergy[kMaxTaps];

    void Init()
    {
      writeIndex = 0;
      for (int i = 0; i < kMaxTaps; i++)
      {
        tapTime[i] = (float)(i + 1) / (float)kMaxTaps;
        tapLevel[i] = 1.0f;
        tapPan[i] = 0.0f;
        filterCutoff[i] = 0.8f; // ~16kHz
        filterQ[i] = 0.0f;
        filterType[i] = TAP_FILTER_LP;
        filters[i].Init();
        tapEnergy[i] = 0.0f;
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
    addParameter(mMix);
    addParameter(mTapCount);
    addParameter(mSkew);
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

  // --- Filter accessors ---

  float MultitapDelay::getFilterCutoff(int i) { return mpInternal->filterCutoff[CLAMP(0, kMaxTaps - 1, i)]; }
  void MultitapDelay::setFilterCutoff(int i, float v) { mpInternal->filterCutoff[CLAMP(0, kMaxTaps - 1, i)] = CLAMP(0.0f, 1.0f, v); }
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
    mpInternal->filterCutoff[i] = CLAMP(0.0f, 1.0f, mEditFilterCutoff.value());
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

    int maxDelay = mMaxDelayInSamples;
    float sr = globalConfig.sampleRate;

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

    // Update filter coefficients (cheap, no transcendentals)
    for (int t = 0; t < tapCount; t++)
    {
      float cutoff = CLAMP(0.0001f, 0.49f, s.filterCutoff[t]);
      float q = 1.0f + 99.0f * s.filterQ[t] * s.filterQ[t];
      float bandQ = q * (0.5f + cutoff * 2.0f);
      if (bandQ < 0.5f) bandQ = 0.5f;
      s.filters[t].set_f_q<stmlib::FREQUENCY_FAST>(cutoff, bandQ);
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
        int delayInt = (int)delaySamples;
        float delayFrac = delaySamples - (float)delayInt;

        if (delayInt < 1) delayInt = 1;
        if (delayInt >= maxDelay - 1) delayInt = maxDelay - 2;

        // Read with linear interpolation
        int idx0 = (s.writeIndex - delayInt + maxDelay) % maxDelay;
        int idx1 = (idx0 - 1 + maxDelay) % maxDelay;
        float tapOut = buf[idx0] + (buf[idx1] - buf[idx0]) * delayFrac;

        // Apply per-tap filter
        switch (s.filterType[t])
        {
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

        tapOut *= s.tapLevel[t];

        // Energy follower for visualization
        float e = tapOut * tapOut;
        s.tapEnergy[t] += (e - s.tapEnergy[t]) * 0.001f;

        // Pan: pre-cached equal power
        wetL += tapOut * mCachedPanL[t];
        wetR += tapOut * mCachedPanR[t];

        lastTapOut = tapOut;
      }

      // Feedback from last active tap
      buf[s.writeIndex] += lastTapOut * feedback;

      // Advance write index
      s.writeIndex = (s.writeIndex + 1) % maxDelay;

      // Mix
      float mixedL = x * (1.0f - mix) + wetL * mix;
      float mixedR = x * (1.0f - mix) + wetR * mix;

      // Saturation
      if (tanhAmt > 0.001f)
      {
        float drive = 1.0f + tanhAmt * 3.0f;
        mixedL = mixedL * (1.0f - tanhAmt) + tanhf(mixedL * drive) * tanhAmt;
        mixedR = mixedR * (1.0f - tanhAmt) + tanhf(mixedR * drive) * tanhAmt;
      }

      out[i] = mixedL * outputLevel;
      outR[i] = mixedR * outputLevel;
    }
  }

} // namespace stolmine
