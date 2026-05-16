// MultitapDelay -- Rainmaker-inspired multitap delay for ER-301
//
// Per-tap filter bank vectorized to NEON SoA on Cortex-A8. 8 taps × TPT
// SVF (g/r/h/state_1/state_2) packed as 2 NEON quads; mode dispatch
// (LP/BP/HP/Notch/Off) hoisted to block-rate-baked per-tap gain
// coefficients with branchless useFilterMask select. Per-tap level +
// stereo pan + energy follower also lane-parallel in Pass C.
//
// no-tree-vectorize is load-bearing per feedback_neon_hint_surfaces:
// prevents GCC from auto-vectorizing the SoA brace-init and emitting
// :64 hints that trap on Cortex-A8.
#pragma GCC optimize("no-tree-vectorize")

#include "MultitapDelay.h"
#include <od/config.h>
#include <od/extras/BigHeap.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>
#include <new>

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

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

    // Per-tap filter — user-facing params
    float filterCutoff[kMaxTaps]; // 0-1 normalized
    float filterQ[kMaxTaps];      // 0-1
    int filterType[kMaxTaps];
    float cachedBandQ[kMaxTaps]; // for feedback compensation

    // Per-tap filter — NEON SoA SVF bank (replaces stmlib::Svf instances).
    // 8 taps = 2 NEON quads; aligned(16) + heap-allocated (Internal lives
    // on the heap) per feedback_neon_intrinsics_drumvoice. Block-rate
    // setup writes g/r/h; per-sample inner kernel updates state_1/state_2.
    float svfG_[kMaxTaps] __attribute__((aligned(16)));
    float svfR_[kMaxTaps] __attribute__((aligned(16)));
    float svfH_[kMaxTaps] __attribute__((aligned(16)));
    float svfS1_[kMaxTaps] __attribute__((aligned(16)));
    float svfS2_[kMaxTaps] __attribute__((aligned(16)));

    // Block-rate-baked per-tap filter mode gains. Branchless dispatch
    // replaces the per-sample switch on filterType[t]. Notch = lp + hp;
    // useFilterMask = 0 for OFF mode (pass tap through unfiltered).
    float lpGain_[kMaxTaps] __attribute__((aligned(16)));
    float bpGain_[kMaxTaps] __attribute__((aligned(16)));
    float hpGain_[kMaxTaps] __attribute__((aligned(16)));
    float notchGain_[kMaxTaps] __attribute__((aligned(16)));  // 1 for notch, else 0
    float useFilterMask_[kMaxTaps] __attribute__((aligned(16)));

    // Pass-B scratch: per-tap pre-filter output (after buffer read + grain
    // interp). Pass C reads this, runs SVF, writes back the post-filter
    // output. Class-member (heap) to dodge :64 stack-local trap.
    float tapOutScratch_[kMaxTaps] __attribute__((aligned(16)));

    // Pre-cached pan SoA quads (also in Object's mCachedPanL/R but we
    // need them aligned for NEON loads in Pass C). Refreshed at block
    // rate alongside the tap distribution recompute.
    float panL_[kMaxTaps] __attribute__((aligned(16)));
    float panR_[kMaxTaps] __attribute__((aligned(16)));

    // Pre-cached effective tap level (tapLevel × inactive mask). Muted
    // taps get 0 here so the NEON multiply zeros their contribution.
    float effectiveTapLevel_[kMaxTaps] __attribute__((aligned(16)));

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

    // Per-tap smoothed delaySamples used for grain spawn start position.
    // Smoothing at ~20ms time constant dampens jitter from CV/drift that
    // would otherwise offset successive grain starts and produce inter-
    // grain phase incoherence (audible as warble).
    float smoothedDelaySamples[kMaxTaps];

    // Hann-window LUT for grain envelope -- 0.5 * (1 - cos(2 pi phase)).
    // Retains the sineLUT name for diff minimality but populated with
    // Hann values; Hann is optimal for constant-overlap-add at 75%
    // overlap (our grain spacing), vs the old sin(pi*phase) which
    // produces audible amplitude ripple when grains sum.
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
        cachedBandQ[i] = 0.5f;
        tapEnergy[i] = 0.0f;
        driftPhase[i] = (float)i * 1.618f; // golden ratio spread
        grainSpawnCounter[i] = 0;
        smoothedDelaySamples[i] = 0.0f;
        for (int g = 0; g < kGrainsPerTap; g++)
        {
          grains[i][g].active = false;
          grains[i][g].reverse = false;
        }
        // NEON SoA SVF init: g=0 freezes state, h=1 neutral
        svfG_[i] = 0.0f;
        svfR_[i] = 1.0f;
        svfH_[i] = 1.0f;
        svfS1_[i] = 0.0f;
        svfS2_[i] = 0.0f;
        lpGain_[i] = 0.0f;
        bpGain_[i] = 0.0f;
        hpGain_[i] = 0.0f;
        notchGain_[i] = 0.0f;
        useFilterMask_[i] = 0.0f;
        tapOutScratch_[i] = 0.0f;
        panL_[i] = 0.707f;
        panR_[i] = 0.707f;
        effectiveTapLevel_[i] = 0.0f;
      }
      // Pre-compute Hann window LUT: 0.5 * (1 - cos(2 pi t)).
      for (int i = 0; i < kSineLUTSize; i++)
      {
        float t = (float)i / (float)(kSineLUTSize - 1);
        sineLUT[i] = 0.5f * (1.0f - cosf(t * 6.28318530718f));
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
    int stackExp = CLAMP(0, 3, (int)(mStack.value() + 0.5f));
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
    float maxBufTime = (float)mMaxDelayInSamples / globalConfig.sampleRate;
    auto rndAllTopLevel = [&]() {
      rndBias(mBiasMasterTime, 0.01f, maxBufTime);
      rndBias(mBiasFeedback, 0.0f, 0.95f);
      rndBias(mBiasFeedbackTone, -1.0f, 1.0f);
      rndBias(mBiasSkew, -2.0f, 2.0f);
      rndBias(mBiasGrainSize, 0.0f, 1.0f);
      rndBias(mBiasDrift, 0.0f, 1.0f);
      rndBias(mBiasReverse, 0.0f, 1.0f);
      rndBiasInt(mBiasStack, 0.0f, 3.0f);
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
    case 10: rndBias(mBiasMasterTime, 0.01f, maxBufTime); break;
    case 11: rndBias(mBiasFeedback, 0.0f, 0.95f); break;
    case 12: rndBias(mBiasFeedbackTone, -1.0f, 1.0f); break;
    case 13: rndBias(mBiasSkew, -2.0f, 2.0f); break;
    case 14: rndBias(mBiasGrainSize, 0.0f, 1.0f); break;
    case 15: rndBias(mBiasDrift, 0.0f, 1.0f); break;
    case 16: rndBias(mBiasReverse, 0.0f, 1.0f); break;
    case 17: rndBiasInt(mBiasStack, 0.0f, 3.0f); break;
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

    float maxBufferTime = (float)mMaxDelayInSamples / globalConfig.sampleRate;
    float masterTimeRaw = CLAMP(0.001f, maxBufferTime, mMasterTime.value());
    float feedback = CLAMP(0.0f, 0.95f, mFeedback.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float inputLevel = CLAMP(0.0f, 4.0f, mInputLevel.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float tanhAmt = CLAMP(0.0f, 1.0f, mTanhAmt.value());
    float skew = mSkew.value();
    float drift = CLAMP(0.0f, 1.0f, mDrift.value());
    float reverse = CLAMP(0.0f, 1.0f, mReverse.value());
    int stackExp = CLAMP(0, 3, (int)(mStack.value() + 0.5f));
    int stack = 1 << stackExp; // 1, 2, 4, 8
    if (stack > tapCount) stack = tapCount;
    int gridExp = CLAMP(0, 4, (int)(mGrid.value() + 0.5f));
    int grid = 1 << gridExp; // 1, 2, 4, 8, 16
    float grainSizeParam = CLAMP(0.0f, 1.0f, mGrainSize.value());

    int maxDelay = mMaxDelayInSamples;
    float sr = globalConfig.sampleRate;

    // V/Oct master pitch (ConstantOffset outputs 0.1 per octave in 10Vpp range)
    float voctPitch = mVOctPitch.value() * 10.0f; // scale to octaves

    // Grain size: 0=5ms, 0.5=150ms, 1.0=300ms. Max bumped from 100ms
    // to 300ms to cover low-frequency pitched content with enough
    // cycles per grain (avoids amplitude flutter on bass material).
    int grainDuration = (int)(sr * (0.005f + grainSizeParam * 0.295f));
    if (grainDuration < 64) grainDuration = 64;
    // 50% overlap. With Hann envelope this is the COLA sweet spot --
    // successive grains sum to flat amplitude, killing envelope-induced
    // flutter. Fits the existing 3-grain-per-tap array (max 2 live at
    // any moment plus 1 spare for transitions).
    int grainPeriod = (int)(grainDuration * 0.5f);
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

    // Block-rate filter coefficient + mode bake. Writes SVF TPT
    // coefficients (g/r/h) and mode gain coefficients directly into
    // Internal's SoA arrays for NEON Pass C. Replaces the per-tap
    // stmlib::Svf::set_f_q calls (mathematically identical, same
    // FREQUENCY_FAST tan approximation: tan(f) ≈ f + f³/3).
    for (int t = 0; t < tapCount; t++)
    {
      // filterCutoff stored as Hz (20-20000), convert to normalized
      float cutoffHz = CLAMP(20.0f, 10000.0f, s.filterCutoff[t]);
      float freq = cutoffHz / sr;
      freq = CLAMP(0.0001f, 0.49f, freq);
      float q = 1.0f + 29.0f * s.filterQ[t] * s.filterQ[t]; // 1 to 30
      float bandQ = q * (0.5f + freq * 2.0f);
      if (bandQ < 0.5f) bandQ = 0.5f;
      s.cachedBandQ[t] = bandQ;

      // TPT SVF coefficients (mirror stmlib::OnePole::tan FREQUENCY_FAST)
      float g = freq * (1.0f + freq * freq * 0.333333333f);
      float r = 1.0f / bandQ;
      s.svfG_[t] = g;
      s.svfR_[t] = r;
      s.svfH_[t] = 1.0f / (1.0f + r * g + g * g);

      // Mode gain bake — branchless dispatch replaces the per-sample switch
      int ft = s.filterType[t];
      s.lpGain_[t]    = (ft == TAP_FILTER_LP    || ft == TAP_FILTER_NOTCH) ? 1.0f : 0.0f;
      s.bpGain_[t]    = (ft == TAP_FILTER_BP)                              ? 1.0f : 0.0f;
      s.hpGain_[t]    = (ft == TAP_FILTER_HP    || ft == TAP_FILTER_NOTCH) ? 1.0f : 0.0f;
      s.notchGain_[t] = 0.0f;  // notch handled via lp+hp combo above
      s.useFilterMask_[t] = (ft != TAP_FILTER_OFF) ? 1.0f : 0.0f;

      // Effective per-tap level (used in NEON Pass C). Muted-tap mask
      // is folded in via the sample-rate `if (tapLevel < 0.001) continue`
      // path in Pass B which zeros tapOutScratch_; this fold is just the
      // amplitude scaling. Pan cached SoA-aligned for NEON loads.
      s.effectiveTapLevel_[t] = s.tapLevel[t];
      s.panL_[t] = mCachedPanL[t];
      s.panR_[t] = mCachedPanR[t];
    }
    // Pad unused lanes to neutral (g=0 freezes state, zero contribution)
    for (int t = tapCount; t < kMaxTaps; t++)
    {
      s.svfG_[t] = 0.0f;
      s.svfR_[t] = 1.0f;
      s.svfH_[t] = 1.0f;
      s.lpGain_[t] = 0.0f;
      s.bpGain_[t] = 0.0f;
      s.hpGain_[t] = 0.0f;
      s.notchGain_[t] = 0.0f;
      s.useFilterMask_[t] = 0.0f;
      s.effectiveTapLevel_[t] = 0.0f;
      s.panL_[t] = 0.0f;
      s.panR_[t] = 0.0f;
    }

    // Last active tap index — for feedback extraction after Pass C
    int lastActiveTapIdx = 0;
    for (int t = tapCount - 1; t >= 0; t--)
    {
      if (s.tapLevel[t] >= 0.001f) { lastActiveTapIdx = t; break; }
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

      // Pre-pass: smooth the delaySamples used for grain spawn (20ms
      // time constant; tames CV / drift jitter so successive grain
      // starts stay phase-coherent) AND fire one prefetch per active
      // tap upfront. tapCount is small (<= kMaxTaps = 8) so the memory
      // system can service all cache misses in parallel during the
      // first iteration's work. Strictly more effective than the old
      // 1-ahead in-loop prefetch on a short loop, and the buffer here
      // can be up to 20s (~960 KB) so memory latency dominates.
      int32_t prefIdx[kMaxTaps];
      const float dsSmoothCoeff = 0.001f; // ~20ms @ 48 kHz
      for (int t = 0; t < tapCount; t++)
      {
        s.smoothedDelaySamples[t] +=
            (mCachedDelaySamples[t] - s.smoothedDelaySamples[t]) * dsSmoothCoeff;
        float p = (float)s.writeIndex - mCachedDelaySamples[t];
        if (p < 0.0f) p += (float)maxDelay;
        prefIdx[t] = (int)p;
        if (s.tapLevel[t] >= 0.001f)
          __builtin_prefetch(&buf[prefIdx[t]], 0, 1);
      }

      // ---- Pass B: per-tap scalar gather (grain machinery + buffer reads)
      // Each tap reads from a different scattered location in the delay
      // buffer — fundamentally scalar on Cortex-A8 (no NEON gather load
      // per feedback_neon_no_gather_lut_dsp). Writes pre-filter tap
      // output into tapOutScratch_[t] for Pass C to consume.
      for (int t = 0; t < tapCount; t++)
      {
        if (s.tapLevel[t] < 0.001f)
        {
          s.tapOutScratch_[t] = 0.0f;
          continue;
        }

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
                // Use smoothed delaySamples so successive grain starts
                // don't jitter with CV/drift, which would produce inter-
                // grain phase incoherence and amplitude modulation.
                float startPos = (float)s.writeIndex - s.smoothedDelaySamples[t];
                while (startPos < 0.0f) startPos += (float)maxDelay;
                s.grains[t][g].readPos = startPos;
                break;
              }
            }
            s.grainSpawnCounter[t] = grainPeriod;
          }
          // Prefetch pre-pass: fire one prefetch per active grain up
          // front so the memory system can service the 2-3 concurrent
          // misses in parallel while the envelope lookup / arithmetic
          // for the first grain runs.
          int grainIdx[kGrainsPerTap];
          for (int g = 0; g < kGrainsPerTap; g++)
          {
            if (!s.grains[t][g].active) { grainIdx[g] = -1; continue; }
            int idx = (int)s.grains[t][g].readPos;
            if (idx >= maxDelay) idx -= maxDelay;
            grainIdx[g] = idx;
            __builtin_prefetch(&buf[idx], 0, 1);
          }

          for (int g = 0; g < kGrainsPerTap; g++)
          {
            Internal::TapGrain &gr = s.grains[t][g];
            if (!gr.active) continue;
            float env = s.lookupSine(gr.phase);
            // readPos is kept in [0, maxDelay) by the advance wrap below,
            // so we can skip the modulo and floorf that were previously
            // here -- (int) cast is floor for non-negative values, and
            // integer modulo is an actual division on am335x (~15 cycles)
            // which we don't want per grain per sample.
            int idx = grainIdx[g];
            float frac = gr.readPos - (float)idx;
            int idx2 = idx + 1;
            if (idx2 >= maxDelay) idx2 = 0;
            float s0 = bufRead(buf, idx);
            float s1 = bufRead(buf, idx2);
            tapOut += (s0 + (s1 - s0) * frac) * env;
            gr.readPos += gr.reverse ? -gr.speed : gr.speed;
            if (gr.readPos < 0.0f) gr.readPos += (float)maxDelay;
            if (gr.readPos >= (float)maxDelay) gr.readPos -= (float)maxDelay;
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

        s.tapOutScratch_[t] = tapOut;
      }
      // Zero out unused lanes (taps tapCount..kMaxTaps-1) so NEON Pass C
      // sees clean zeros instead of stale state.
      for (int t = tapCount; t < kMaxTaps; t++)
        s.tapOutScratch_[t] = 0.0f;

      // ---- Pass C: NEON SoA filter bank (8 taps × TPT SVF in 2 quads)
      // + branchless mode dispatch + level × pan accumulate. Mirrors
      // the SoA SVF kernels in Filterbank, Rings modal, and Impasto.
      // Lane 3 of each quad either holds a real tap or is padding (g=0,
      // useFilterMask=0, effectiveTapLevel=0 → contributes 0).
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
      float32x4_t wetLAccQ = vdupq_n_f32(0.0f);
      float32x4_t wetRAccQ = vdupq_n_f32(0.0f);

      for (int q = 0; q < 2; q++)
      {
        int o = q * 4;
        float32x4_t tapInQ = vld1q_f32(&s.tapOutScratch_[o]);
        float32x4_t gQ  = vld1q_f32(&s.svfG_[o]);
        float32x4_t rQ  = vld1q_f32(&s.svfR_[o]);
        float32x4_t hQ  = vld1q_f32(&s.svfH_[o]);
        float32x4_t s1Q = vld1q_f32(&s.svfS1_[o]);
        float32x4_t s2Q = vld1q_f32(&s.svfS2_[o]);

        // TPT SVF: hp = (in - r·s1 - g·s1 - s2) · h
        float32x4_t hpQ = vmulq_f32(
            vsubq_f32(
                vsubq_f32(
                    vsubq_f32(tapInQ, vmulq_f32(rQ, s1Q)),
                    vmulq_f32(gQ, s1Q)),
                s2Q),
            hQ);
        // bp = s1 + g·hp;  s1' = bp + g·hp
        float32x4_t bpQ    = vmlaq_f32(s1Q, gQ, hpQ);
        float32x4_t newS1Q = vmlaq_f32(bpQ, gQ, hpQ);
        // lp = s2 + g·bp;  s2' = lp + g·bp
        float32x4_t lpQ    = vmlaq_f32(s2Q, gQ, bpQ);
        float32x4_t newS2Q = vmlaq_f32(lpQ, gQ, bpQ);
        vst1q_f32(&s.svfS1_[o], newS1Q);
        vst1q_f32(&s.svfS2_[o], newS2Q);

        // Mode mix: branchless dispatch via baked gains. NOTCH = lp + hp
        // (handled by setting both lpGain_ and hpGain_ to 1 for notch
        // taps in the block-rate setup).
        float32x4_t lpGQ = vld1q_f32(&s.lpGain_[o]);
        float32x4_t bpGQ = vld1q_f32(&s.bpGain_[o]);
        float32x4_t hpGQ = vld1q_f32(&s.hpGain_[o]);
        float32x4_t filteredQ = vmlaq_f32(
            vmlaq_f32(vmulq_f32(lpQ, lpGQ), bpQ, bpGQ),
            hpQ, hpGQ);

        // Branchless OFF mode: final = useFilter ? filtered : tapIn
        float32x4_t maskQ = vld1q_f32(&s.useFilterMask_[o]);
        float32x4_t finalQ = vmlaq_f32(tapInQ, maskQ,
                                       vsubq_f32(filteredQ, tapInQ));

        // Apply effective per-tap level (folds muted-tap zeroing)
        float32x4_t levelQ = vld1q_f32(&s.effectiveTapLevel_[o]);
        float32x4_t filteredOutQ = vmulq_f32(finalQ, levelQ);
        // Store back so feedback path can read post-filter post-level value
        vst1q_f32(&s.tapOutScratch_[o], filteredOutQ);

        // Pan accumulate: wetL += filteredOut · panL;  wetR += · panR
        float32x4_t panLQ = vld1q_f32(&s.panL_[o]);
        float32x4_t panRQ = vld1q_f32(&s.panR_[o]);
        wetLAccQ = vmlaq_f32(wetLAccQ, filteredOutQ, panLQ);
        wetRAccQ = vmlaq_f32(wetRAccQ, filteredOutQ, panRQ);
      }

      // Horizontal sum the wet accumulators (4 lanes each, both quads)
      wetL = vgetq_lane_f32(wetLAccQ, 0) + vgetq_lane_f32(wetLAccQ, 1)
           + vgetq_lane_f32(wetLAccQ, 2) + vgetq_lane_f32(wetLAccQ, 3);
      wetR = vgetq_lane_f32(wetRAccQ, 0) + vgetq_lane_f32(wetRAccQ, 1)
           + vgetq_lane_f32(wetRAccQ, 2) + vgetq_lane_f32(wetRAccQ, 3);
#else
      // Scalar fallback (linux x86, darwin). Same math, 8 lanes.
      for (int t = 0; t < kMaxTaps; t++)
      {
        float tapIn = s.tapOutScratch_[t];
        float g = s.svfG_[t], r = s.svfR_[t], h = s.svfH_[t];
        float hp = (tapIn - r * s.svfS1_[t] - g * s.svfS1_[t] - s.svfS2_[t]) * h;
        float bp = s.svfS1_[t] + g * hp;
        s.svfS1_[t] = bp + g * hp;
        float lp = s.svfS2_[t] + g * bp;
        s.svfS2_[t] = lp + g * bp;
        float filtered = lp * s.lpGain_[t] + bp * s.bpGain_[t] + hp * s.hpGain_[t];
        float final = tapIn + s.useFilterMask_[t] * (filtered - tapIn);
        float filteredOut = final * s.effectiveTapLevel_[t];
        s.tapOutScratch_[t] = filteredOut;
        wetL += filteredOut * s.panL_[t];
        wetR += filteredOut * s.panR_[t];
      }
#endif

      // Energy follower for viz — scalar, only across active taps.
      // tapEnergy isn't aligned(16) so we don't NEON-load it; cost is
      // sub-1pp either way.
      for (int t = 0; t < tapCount; t++)
      {
        float e = s.tapOutScratch_[t] * s.tapOutScratch_[t];
        s.tapEnergy[t] += (e - s.tapEnergy[t]) * 0.001f;
      }

      // Feedback: pick last active tap's post-filter output with
      // Q-compensation (resonance audible but doesn't accumulate).
      lastTapOut = s.tapOutScratch_[lastActiveTapIdx]
                 / (1.0f + s.cachedBandQ[lastActiveTapIdx] * 0.1f);

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
