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

  static inline float lcgFloat(uint32_t &seed)
  {
    seed = seed * 1103515245u + 12345u;
    return (float)((seed >> 8) & 0x7FFF) / 32767.0f;
  }

  // Fast log2 via IEEE 754 bit extraction
  static inline float fast_log2(float x)
  {
    union { float f; int32_t i; } v;
    v.f = x;
    float exponent = (float)((v.i >> 23) - 127);
    v.i = (v.i & 0x7FFFFF) | (127 << 23);
    return exponent + v.f * (v.f * -0.3333f + 2.0f) - 1.667f;
  }

  // Fast exp2 via IEEE 754 bit packing
  static inline float fast_exp2(float x)
  {
    float xi = floorf(x);
    float xf = x - xi;
    float m = 1.0f + xf * (0.6602f + xf * 0.3398f);
    union { float f; int32_t i; } v;
    v.i = ((int32_t)xi + 127) << 23;
    return v.f * m;
  }

  // Fast powf approximation: x^y = exp2(y * log2(x))
  static inline float fast_powf(float base, float exponent)
  {
    if (base <= 0.0f) return 0.0f;
    return fast_exp2(exponent * fast_log2(base));
  }

  // Fast tanh approximation (Pade 3/3, < 0.1% error for |x| < 4, clamps beyond)
  static inline float fast_tanh(float x)
  {
    if (x < -4.0f) return -1.0f;
    if (x >  4.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

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

    // Per-tap drift offsets (smoothly varying random time jitter)
    float driftPhase[kMaxTaps];

    // Granular pitch shift state
    struct TapGrain
    {
      float phase;    // 0-1 through envelope
      float readPos;  // fractional sample position in buffer
      float phaseDelta;
      float speed;
      bool active;
      bool reverse;
    };
    TapGrain grains[kMaxTaps][kGrainsPerTap];
    int grainSpawnCounter[kMaxTaps];

    // Sine LUT for grain envelope (avoid sinf in inner loop)
    float sineLUT[kSineLUTSize];

    // Fast RNG (replaces rand() in inner loop)
    uint32_t rngSeed = 42;

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
        driftPhase[i] = (float)i * 1.618f; // golden ratio spread
        grainSpawnCounter[i] = 0;
        for (int g = 0; g < kGrainsPerTap; g++)
        {
          grains[i][g].active = false;
          grains[i][g].reverse = false;
        }
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
    addInput(mXformGate);
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
    addParameter(mDrift);
    addParameter(mReverse);
    addParameter(mStack);
    addParameter(mGrid);
    addParameter(mXformTarget);
    addParameter(mXformDepth);
    addParameter(mXformSpread);
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

  static inline void bufWrite(int16_t *buf, int idx, float v)
  {
    int s = (int)(v * 32767.0f);
    if (s > 32767) s = 32767;
    if (s < -32767) s = -32767;
    buf[idx] = (int16_t)s;
  }

  static inline float bufRead(const int16_t *buf, int idx)
  {
    return (float)buf[idx] * (1.0f / 32767.0f);
  }

  bool MultitapDelay::allocate(int Ns)
  {
    deallocate();
    int nbytes = Ns * sizeof(int16_t);
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

  void MultitapDelay::setMono(bool mono) { mMono = mono; }

  float MultitapDelay::maximumDelayTime()
  {
    return mMaxDelayInSamples * globalConfig.samplePeriod;
  }

  float MultitapDelay::getMaxBeatTime()
  {
    int tapCount = CLAMP(1, kMaxTaps, (int)(mTapCount.value() + 0.5f));
    int stackExp = CLAMP(0, 4, (int)(mStack.value() + 0.5f));
    int stack = 1 << stackExp;
    if (stack > tapCount) stack = tapCount;
    int gridExp = CLAMP(0, 4, (int)(mGrid.value() + 0.5f));
    int grid = 1 << gridExp;
    int numGroups = (tapCount + stack - 1) / stack;
    if (numGroups < 1) numGroups = 1;
    float maxDelaySec = mMaxDelayInSamples * globalConfig.samplePeriod;
    return maxDelaySec * (float)grid / (float)numGroups;
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

  void MultitapDelay::fireRandomize()
  {
    mManualFire = true;
  }

  void MultitapDelay::setTopLevelBias(int which, od::Parameter *param)
  {
    switch (which)
    {
    case 0: mBiasMasterTime = param; break;
    case 1: mBiasFeedback = param; break;
    case 2: mBiasFeedbackTone = param; break;
    case 3: mBiasSkew = param; break;
    case 4: mBiasGrainSize = param; break;
    case 5: mBiasDrift = param; break;
    case 6: mBiasReverse = param; break;
    case 7: mBiasStack = param; break;
    case 8: mBiasGrid = param; break;
    case 9: mBiasTapCount = param; break;
    }
  }

  static float randomizeValue(float cur, float mn, float mx, float depth, float spread)
  {
    float range = mx - mn;
    float center = spread * (mn + mx) * 0.5f + (1.0f - spread) * cur;
    float dev = depth * range * 0.5f;
    float r = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    float v = center + r * dev;
    return CLAMP(mn, mx, v);
  }

  void MultitapDelay::applyRandomize()
  {
    Internal &s = *mpInternal;
    int target = CLAMP(0, 16, (int)(mXformTarget.value() + 0.5f));
    float depth = CLAMP(0.0f, 1.0f, mXformDepth.value());
    float spread = CLAMP(0.0f, 1.0f, mXformSpread.value());
    int n = mCachedTapCount;

    // Helper lambdas
    auto rndLevels = [&]() { for (int i = 0; i < n; i++) s.tapLevel[i] = randomizeValue(s.tapLevel[i], 0, 1, depth, spread); };
    auto rndPans = [&]() { for (int i = 0; i < n; i++) s.tapPan[i] = randomizeValue(s.tapPan[i], -1, 1, depth, spread); };
    auto rndPitch = [&]() { for (int i = 0; i < n; i++) s.tapPitch[i] = floorf(randomizeValue(s.tapPitch[i], -24, 24, depth, spread) + 0.5f); };
    auto rndCutoff = [&]() { for (int i = 0; i < n; i++) s.filterCutoff[i] = randomizeValue(s.filterCutoff[i], 20, 10000, depth, spread); };
    auto rndQ = [&]() { for (int i = 0; i < n; i++) s.filterQ[i] = randomizeValue(s.filterQ[i], 0, 1, depth, spread); };
    auto rndType = [&]() { for (int i = 0; i < n; i++) s.filterType[i] = CLAMP(0, (int)TAP_FILTER_COUNT - 1, (int)(randomizeValue((float)s.filterType[i], 0, TAP_FILTER_COUNT - 1, depth, spread) + 0.5f)); };

    // Helpers for top-level param randomization via stored Bias refs
    auto rndBias = [&](od::Parameter *p, float mn, float mx) {
      if (p) p->hardSet(randomizeValue(p->value(), mn, mx, depth, spread));
    };
    auto rndBiasInt = [&](od::Parameter *p, float mn, float mx) {
      if (p) p->hardSet(floorf(randomizeValue(p->value(), mn, mx, depth, spread) + 0.5f));
    };
    auto rndAllTopLevel = [&]() {
      rndBias(mBiasMasterTime, 0.01f, 20.0f);
      rndBias(mBiasFeedback, 0.0f, 0.95f);
      rndBias(mBiasFeedbackTone, -1.0f, 1.0f);
      rndBias(mBiasSkew, -2.0f, 2.0f);
      rndBias(mBiasGrainSize, 0.0f, 1.0f);
      rndBias(mBiasDrift, 0.0f, 1.0f);
      rndBias(mBiasReverse, 0.0f, 1.0f);
      rndBiasInt(mBiasStack, 0.0f, 4.0f);
      rndBiasInt(mBiasGrid, 0.0f, 4.0f);
      rndBiasInt(mBiasTapCount, 1.0f, 8.0f);
    };
    auto resetAllTopLevel = [&]() {
      if (mBiasMasterTime) mBiasMasterTime->hardSet(0.5f);
      if (mBiasFeedback) mBiasFeedback->hardSet(0.3f);
      if (mBiasFeedbackTone) mBiasFeedbackTone->hardSet(0.0f);
      if (mBiasSkew) mBiasSkew->hardSet(0.0f);
      if (mBiasGrainSize) mBiasGrainSize->hardSet(0.5f);
      if (mBiasDrift) mBiasDrift->hardSet(0.0f);
      if (mBiasReverse) mBiasReverse->hardSet(0.0f);
      if (mBiasStack) mBiasStack->hardSet(0.0f);
      if (mBiasGrid) mBiasGrid->hardSet(0.0f);
      if (mBiasTapCount) mBiasTapCount->hardSet(4.0f);
    };

    switch (target)
    {
    case 0: // all
      rndLevels(); rndPans(); rndPitch(); rndCutoff(); rndQ(); rndType();
      rndAllTopLevel();
      break;
    case 1: rndLevels(); rndPans(); rndPitch(); break;  // taps
    case 2: rndAllTopLevel(); break;                      // delay
    case 3: rndCutoff(); rndQ(); rndType(); break;       // filters
    case 4: rndLevels(); break;
    case 5: rndPans(); break;
    case 6: rndPitch(); break;
    case 7: rndCutoff(); break;
    case 8: rndQ(); break;
    case 9: rndType(); break;
    case 10: rndBias(mBiasMasterTime, 0.01f, 20.0f); break;
    case 11: rndBias(mBiasFeedback, 0.0f, 0.95f); break;
    case 12: rndBias(mBiasFeedbackTone, -1.0f, 1.0f); break;
    case 13: rndBias(mBiasSkew, -2.0f, 2.0f); break;
    case 14: rndBias(mBiasGrainSize, 0.0f, 1.0f); break;
    case 15: rndBias(mBiasDrift, 0.0f, 1.0f); break;
    case 16: rndBias(mBiasReverse, 0.0f, 1.0f); break;
    case 17: rndBiasInt(mBiasStack, 0.0f, 4.0f); break;
    case 18: rndBiasInt(mBiasGrid, 0.0f, 4.0f); break;
    case 19: rndBiasInt(mBiasTapCount, 1.0f, 8.0f); break;
    case 20: // reset
      for (int i = 0; i < kMaxTaps; i++)
      {
        s.tapLevel[i] = 1.0f; s.tapPan[i] = 0.0f; s.tapPitch[i] = 0.0f;
        s.filterCutoff[i] = 10000.0f; s.filterQ[i] = 0.0f; s.filterType[i] = TAP_FILTER_OFF;
      }
      resetAllTopLevel();
      break;
    }

    loadTap(mLastLoadedTap);
    loadFilter(mLastLoadedFilter);
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
    int16_t *buf = (int16_t *)s.buffer;

    int tapCount = CLAMP(1, kMaxTaps, (int)(mTapCount.value() + 0.5f));
    mCachedTapCount = tapCount;

    float masterTimeRaw = CLAMP(0.001f, 20.0f, mMasterTime.value());
    float feedback = CLAMP(0.0f, 0.95f, mFeedback.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float inputLevel = CLAMP(0.0f, 4.0f, mInputLevel.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float tanhAmt = CLAMP(0.0f, 1.0f, mTanhAmt.value());
    float skew = mSkew.value();
    float drift = CLAMP(0.0f, 1.0f, mDrift.value());
    float reverse = CLAMP(0.0f, 1.0f, mReverse.value());
    int stackExp = CLAMP(0, 4, (int)(mStack.value() + 0.5f));
    int stack = 1 << stackExp; // 1, 2, 4, 8, 16
    if (stack > tapCount) stack = tapCount;
    int gridExp = CLAMP(0, 4, (int)(mGrid.value() + 0.5f));
    int grid = 1 << gridExp; // 1, 2, 4, 8, 16
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

    // Tap distribution: each tap at masterTime * (groupIndex + 1) / grid
    // grid=1: taps at 1x, 2x, 3x... masterTime (widely spaced)
    // grid=16: taps at 1/16, 2/16... masterTime (tightly spaced)
    int numGroups = (tapCount + stack - 1) / stack;
    if (numGroups < 1) numGroups = 1;
    // Total delay span: masterTime * numGroups / grid
    float totalSpan = masterTimeRaw * (float)numGroups / (float)grid;
    float maxSpanSec = (float)maxDelay * globalConfig.samplePeriod;
    if (totalSpan > maxSpanSec) totalSpan = maxSpanSec;
    float masterTime = totalSpan * (float)grid / (float)numGroups;

    // Recompute tap distribution when params change, or every frame if drift active
    bool distDirty = (tapCount != mLastTapCount || skew != mLastSkew
                      || masterTime != mLastMasterTime || drift != mLastDrift
                      || stack != mLastStack || grid != mLastGrid
                      || drift > 0.0f);
    if (distDirty)
    {
      float skewExp = fast_exp2(skew);
      for (int t = 0; t < tapCount; t++)
      {
        int groupIndex = t / stack;
        // Distribute taps: each at masterTime * (groupIndex+1) / grid
        float pos = fast_powf((float)(groupIndex + 1) / (float)grid, skewExp);
        // Drift: per-tap slow sinusoidal offset
        if (drift > 0.001f)
        {
          s.driftPhase[t] += 0.0003f + 0.0001f * (float)t;
          // Fast sine for drift LFO (range reduce + 5th order polynomial)
          float dp = s.driftPhase[t];
          float n = floorf(dp * 0.31831f + 0.5f); // 1/pi
          float xr = dp - n * 3.14159f;
          float x2 = xr * xr;
          float sn = xr * (1.0f + x2 * (-0.16605f + x2 * 0.00761f));
          if ((int)n & 1) sn = -sn;
          float offset = sn * drift * 0.1f;
          pos += offset;
        }
        if (pos < 0.001f) pos = 0.001f;
        s.tapTime[t] = pos;
        mCachedDelaySamples[t] = pos * masterTime * sr;
        // Clamp to buffer
        if (mCachedDelaySamples[t] > (float)(maxDelay - 1))
          mCachedDelaySamples[t] = (float)(maxDelay - 1);
      }
      // Pre-cache pan coefficients (sqrt equal power, no trig)
      for (int t = 0; t < tapCount; t++)
      {
        float p = (s.tapPan[t] + 1.0f) * 0.5f; // 0=left, 1=right
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        mCachedPanL[t] = sqrtf(1.0f - p);
        mCachedPanR[t] = sqrtf(p);
      }
      // Only reload edit buffer when discrete params change, not on drift frames
      bool paramsChanged = (tapCount != mLastTapCount || skew != mLastSkew || masterTime != mLastMasterTime || stack != mLastStack || grid != mLastGrid);
      mLastTapCount = tapCount;
      mLastSkew = skew;
      mLastMasterTime = masterTime;
      mLastDrift = drift;
      mLastStack = stack;
      mLastGrid = grid;
      if (paramsChanged)
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

    float fbNorm = feedback / (1.0f + 0.15f * sqrtf((float)tapCount));

    // Xform gate edge detection
    {
      float *xgate = mXformGate.buffer();
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        bool high = xgate[i] > 0.5f;
        if (high && !mXformGateWasHigh)
        {
          applyRandomize();
        }
        mXformGateWasHigh = high;
      }
      if (mManualFire)
      {
        applyRandomize();
        mManualFire = false;
      }
    }

    // Pre-compute per-tap grain speeds (fast_exp2 replaces powf)
    float tapSpeeds[kMaxTaps];
    bool tapNeedGrains[kMaxTaps];
    for (int t = 0; t < tapCount; t++)
    {
      float pitch = voctPitch + s.tapPitch[t] / 12.0f;
      tapNeedGrains[t] = (fabsf(pitch) > 0.001f) || (reverse > 0.001f);
      tapSpeeds[t] = tapNeedGrains[t] ? fast_exp2(pitch) : 1.0f;
    }

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
      bufWrite(buf, s.writeIndex, x);

      float wetL = 0.0f;
      float wetR = 0.0f;
      float lastTapOut = 0.0f;

      for (int t = 0; t < tapCount; t++)
      {
        // Prefetch next tap's buffer position (hides memory latency)
        if (t + 1 < tapCount && s.tapLevel[t + 1] >= 0.001f)
        {
          float nextPos = (float)s.writeIndex - mCachedDelaySamples[t + 1];
          if (nextPos < 0.0f) nextPos += (float)maxDelay;
          __builtin_prefetch(&buf[(int)nextPos], 0, 1);
        }

        if (s.tapLevel[t] < 0.001f)
          continue;

        float delaySamples = mCachedDelaySamples[t];
        float tapOut = 0.0f;

        if (tapNeedGrains[t])
        {
          // Granular pitch shift path
          float tapSpeed = tapSpeeds[t];
          s.grainSpawnCounter[t]--;
          if (s.grainSpawnCounter[t] <= 0)
          {
            for (int g = 0; g < kGrainsPerTap; g++)
            {
              if (!s.grains[t][g].active)
              {
                s.grains[t][g].active = true;
                s.grains[t][g].phase = 0.0f;
                s.grains[t][g].phaseDelta = grainPhaseDelta;
                s.grains[t][g].speed = tapSpeed;
                bool rev = reverse > 0.001f && lcgFloat(s.rngSeed) < reverse;
                s.grains[t][g].reverse = rev;
                float startPos = (float)s.writeIndex - delaySamples;
                while (startPos < 0.0f) startPos += (float)maxDelay;
                s.grains[t][g].readPos = startPos;
                break;
              }
            }
            s.grainSpawnCounter[t] = grainPeriod;
          }
          for (int g = 0; g < kGrainsPerTap; g++)
          {
            Internal::TapGrain &gr = s.grains[t][g];
            if (!gr.active) continue;
            float env = s.lookupSine(gr.phase);
            int idx = ((int)gr.readPos) % maxDelay;
            if (idx < 0) idx += maxDelay;
            float frac = gr.readPos - floorf(gr.readPos);
            int idx2 = (idx + 1) % maxDelay;
            float s0 = bufRead(buf, idx);
            float s1 = bufRead(buf, idx2);
            tapOut += (s0 + (s1 - s0) * frac) * env;
            gr.readPos += gr.reverse ? -gr.speed : gr.speed;
            if (gr.readPos < 0.0f) gr.readPos += (float)maxDelay;
            gr.phase += gr.phaseDelta;
            if (gr.phase >= 1.0f) gr.active = false;
          }
        }
        else
        {
          // Direct buffer read (no pitch shift, no grains -- much cheaper)
          float readPos = (float)s.writeIndex - delaySamples;
          if (readPos < 0.0f) readPos += (float)maxDelay;
          int idx = (int)readPos;
          float frac = readPos - (float)idx;
          if (idx >= maxDelay) idx -= maxDelay;
          int idx2 = idx + 1;
          if (idx2 >= maxDelay) idx2 = 0;
          tapOut = bufRead(buf, idx) + (bufRead(buf, idx2) - bufRead(buf, idx)) * frac;
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
      // Linear feedback -- only soft-clip if signal is hot
      float fbInjection = (fabsf(fbOut) > 1.5f) ? fast_tanh(fbOut) : fbOut;
      bufWrite(buf, s.writeIndex, bufRead(buf, s.writeIndex) + fbInjection);

      // Advance write index
      s.writeIndex = (s.writeIndex + 1) % maxDelay;

      // Mix
      float mixedL = x * (1.0f - mix) + wetL * mix;
      float mixedR = x * (1.0f - mix) + wetR * mix;

      // User saturation
      if (tanhAmt > 0.001f)
      {
        float drive = 1.0f + tanhAmt * 3.0f;
        mixedL = mixedL * (1.0f - tanhAmt) + fast_tanh(mixedL * drive) * tanhAmt;
        mixedR = mixedR * (1.0f - tanhAmt) + fast_tanh(mixedR * drive) * tanhAmt;
      }

      // Output limiter (invisible, always on)
      if (mMono)
      {
        out[i] = fast_tanh((mixedL + mixedR) * 0.5f * outputLevel);
      }
      else
      {
        out[i] = fast_tanh(mixedL * outputLevel);
        outR[i] = fast_tanh(mixedR * outputLevel);
      }
    }
  }

} // namespace stolmine
