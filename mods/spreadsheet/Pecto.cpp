#include "Pecto.h"
#include <od/config.h>
#include <od/AudioThread.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>
#include <new>
#include <stdlib.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define PECTO_HAS_NEON 1
#endif

namespace stolmine
{

  // Fast tanh approximation (Pade 3/3)
  static inline float fast_tanh(float x)
  {
    if (x < -4.0f) return -1.0f;
    if (x >  4.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

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

  // Linear interpolation read for fractional delay
  static inline float bufReadInterp(const int16_t *buf, float pos, int maxDelay)
  {
    int idx0 = (int)floorf(pos);
    float frac = pos - (float)idx0;
    if (idx0 < 0) idx0 += maxDelay;
    if (idx0 >= maxDelay) idx0 -= maxDelay;
    int idx1 = idx0 + 1;
    if (idx1 >= maxDelay) idx1 = 0;
    float a = bufRead(buf, idx0);
    float b = bufRead(buf, idx1);
    return a + (b - a) * frac;
  }

  struct Pecto::Internal
  {
    char *buffer = 0;
    int writeIndex = 0;
    float tapPosition[kMaxCombTaps];
    float tapWeight[kMaxCombTaps];
    float fbFilterState = 0.0f;
    float sitarEnvFollower = 0.0f;
    float dcX1 = 0.0f;
    float dcY1 = 0.0f;

    void Init()
    {
      writeIndex = 0;
      fbFilterState = 0.0f;
      sitarEnvFollower = 0.0f;
      dcX1 = 0.0f;
      dcY1 = 0.0f;
      for (int i = 0; i < kMaxCombTaps; i++)
      {
        tapPosition[i] = (float)(i + 1) / (float)kMaxCombTaps;
        tapWeight[i] = 1.0f;
      }
    }
  };

  Pecto::Pecto()
  {
    addInput(mIn);
    addInput(mVOct);
    addInput(mXformGate);
    addOutput(mOut);
    addParameter(mCombSize);
    addParameter(mFeedback);
    addParameter(mVOctPitch);
    addParameter(mDensity);
    addParameter(mPattern);
    addParameter(mSlope);
    addParameter(mResonatorType);
    addParameter(mMix);
    addParameter(mInputLevel);
    addParameter(mOutputLevel);
    addParameter(mTanhAmt);
    addParameter(mXformTarget);
    addParameter(mXformDepth);

    mpInternal = new Internal();
    mpInternal->Init();
    memset(mCachedDelaySamples, 0, sizeof(mCachedDelaySamples));
    memset(mCachedDelaySamples0, 0, sizeof(mCachedDelaySamples0));
    for (int i = 0; i < kMaxCombTaps; i++)
      mCachedTapWeight[i] = 1.0f;
    // Snap-and-fade zipper-noise smoother. 25ms ramp at frame rate
    // (matches od::Delay::Delay's `frameRate * 0.025f` setting). The
    // ramp counts down per process() call; getInterpolatedFrame() fills
    // FRAMELENGTH per-sample weights interpolating from current value
    // toward goal.
    mFade.setLength((int)(globalConfig.frameRate * 0.025f));
  }

  Pecto::~Pecto()
  {
    deallocate();
    delete mpInternal;
  }

  // --- Buffer allocation ---

  bool Pecto::allocate(int Ns)
  {
    deallocate();
    int nbytes = Ns * sizeof(int16_t);
    mpInternal->buffer = new (std::nothrow) char[nbytes];
    if (mpInternal->buffer)
      memset(mpInternal->buffer, 0, nbytes);
    return mpInternal->buffer != 0;
  }

  void Pecto::deallocate()
  {
    if (mpInternal->buffer)
    {
      delete[] mpInternal->buffer;
      mpInternal->buffer = 0;
    }
  }

  float Pecto::allocateTimeUpTo(float seconds)
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

  float Pecto::maximumDelayTime()
  {
    return mMaxDelayInSamples * globalConfig.samplePeriod;
  }

  // --- Tap distribution ---

  // Base pattern: compute tap positions as fractions of comb size (0,1]
  static void computePattern(float *pos, int density, int basePattern)
  {
    int N = density;
    switch (basePattern)
    {
    default:
    case 0: // Uniform -- evenly spaced
      for (int i = 0; i < N; i++)
        pos[i] = (float)(i + 1) / (float)N;
      break;

    case 1: // Fibonacci -- golden ratio spacing, sorted
    {
      float phi = 1.6180339887f;
      for (int i = 0; i < N; i++)
        pos[i] = fmodf((float)(i + 1) * (1.0f / phi), 1.0f);
      // Sort ascending
      for (int i = 0; i < N - 1; i++)
        for (int j = i + 1; j < N; j++)
          if (pos[j] < pos[i]) { float tmp = pos[i]; pos[i] = pos[j]; pos[j] = tmp; }
      // Ensure no zero positions
      for (int i = 0; i < N; i++)
        if (pos[i] < 0.001f) pos[i] = 0.001f;
      break;
    }

    case 2: // Early -- clustered toward start (power < 1)
      for (int i = 0; i < N; i++)
      {
        float t = (float)(i + 1) / (float)N;
        pos[i] = powf(t, 0.5f);
      }
      break;

    case 3: // Late -- clustered toward end (power > 1)
      for (int i = 0; i < N; i++)
      {
        float t = (float)(i + 1) / (float)N;
        pos[i] = powf(t, 2.0f);
      }
      break;

    case 4: // Middle -- clustered toward center (sine warp)
      for (int i = 0; i < N; i++)
      {
        float t = (float)(i + 1) / (float)(N + 1);
        pos[i] = 0.5f + 0.5f * sinf((t - 0.5f) * 3.14159f);
      }
      break;

    case 5: // Ess -- S-curve, sparse at ends, dense in middle
      for (int i = 0; i < N; i++)
      {
        float t = (float)(i + 1) / (float)(N + 1);
        float s = t * t * (3.0f - 2.0f * t); // smoothstep
        pos[i] = s;
      }
      break;

    case 6: // Flat -- all taps at same position (unison/chorus)
      for (int i = 0; i < N; i++)
        pos[i] = 1.0f;
      break;

    case 7: // Rev-Fibonacci -- fibonacci mirrored
    {
      float phi = 1.6180339887f;
      for (int i = 0; i < N; i++)
        pos[i] = fmodf((float)(i + 1) * (1.0f / phi), 1.0f);
      for (int i = 0; i < N - 1; i++)
        for (int j = i + 1; j < N; j++)
          if (pos[j] < pos[i]) { float tmp = pos[i]; pos[i] = pos[j]; pos[j] = tmp; }
      // Mirror: reflect around 0.5
      for (int i = 0; i < N; i++)
        pos[i] = 1.0f - pos[i];
      // Re-sort ascending
      for (int i = 0; i < N - 1; i++)
        for (int j = i + 1; j < N; j++)
          if (pos[j] < pos[i]) { float tmp = pos[i]; pos[i] = pos[j]; pos[j] = tmp; }
      for (int i = 0; i < N; i++)
        if (pos[i] < 0.001f) pos[i] = 0.001f;
      break;
    }
    }
  }

  // Seeded perturbation for patterns 8-15
  static void perturbPattern(float *pos, int density, unsigned int seed)
  {
    unsigned int s = seed;
    for (int i = 0; i < density; i++)
    {
      // Simple LCG
      s = s * 1103515245u + 12345u;
      float r = ((float)((s >> 8) & 0x7FFF) / 16383.0f) * 2.0f - 1.0f;
      // Perturb by up to +/-10% of spacing
      float spacing = (i < density - 1) ? (pos[i + 1] - pos[i]) : (1.0f - pos[i]);
      pos[i] += r * spacing * 0.1f;
      if (pos[i] < 0.001f) pos[i] = 0.001f;
      if (pos[i] > 1.0f) pos[i] = 1.0f;
    }
  }

  void __attribute__((optimize("O1"))) Pecto::recomputeTaps(int density, int pattern, int slope)
  {
    Internal &s = *mpInternal;

    int basePattern = pattern & 7;
    computePattern(s.tapPosition, density, basePattern);
    if (pattern >= 8)
      perturbPattern(s.tapPosition, density, (unsigned int)(pattern * 7919 + density * 131));

    // Slope: weight envelope
    for (int i = 0; i < density; i++)
    {
      float t = (density > 1) ? (float)i / (float)(density - 1) : 0.5f;
      switch (slope)
      {
      case 0: // Flat
        s.tapWeight[i] = 1.0f;
        break;
      case 1: // Rise
        s.tapWeight[i] = 0.1f + 0.9f * t;
        break;
      case 2: // Fall
        s.tapWeight[i] = 1.0f - 0.9f * t;
        break;
      case 3: // Rise-Fall (sine hump)
        s.tapWeight[i] = sinf(t * 3.14159f);
        if (s.tapWeight[i] < 0.05f) s.tapWeight[i] = 0.05f;
        break;
      }
    }

    // Sort tap positions ascending and carry weights alongside so the
    // per-block cache population doesn't have to re-sort every block.
    // Multiplying tapPosition[] by a positive scalar (baseDelay) preserves
    // order, so this sort suffices for the lifetime of these tap settings.
    for (int i = 1; i < density; i++)
    {
      float pKey = s.tapPosition[i];
      float wKey = s.tapWeight[i];
      int j = i - 1;
      while (j >= 0 && s.tapPosition[j] > pKey)
      {
        s.tapPosition[j + 1] = s.tapPosition[j];
        s.tapWeight[j + 1] = s.tapWeight[j];
        j--;
      }
      s.tapPosition[j + 1] = pKey;
      s.tapWeight[j + 1] = wKey;
    }

    mLastDensity = density;
    mLastPattern = pattern;
    mLastSlope = slope;
  }

  // --- Xform randomization ---

  static float randomizeValue(float cur, float mn, float mx, float depth)
  {
    // Blend between current value and a random position in range.
    // depth=0: no change, depth=1: fully random across range.
    float r = (float)(rand() & 0x7FFF) / 32767.0f; // 0-1
    float randomPos = mn + r * (mx - mn);
    return cur + (randomPos - cur) * depth;
  }

  void Pecto::fireRandomize() { mManualFire = true; }

  void Pecto::setTopLevelBias(int which, od::Parameter *param)
  {
    switch (which)
    {
    case 0: mBiasCombSize = param; break;
    case 1: mBiasFeedback = param; break;
    case 2: mBiasResonatorType = param; break;
    case 3: mBiasDensity = param; break;
    case 4: mBiasPattern = param; break;
    case 5: mBiasSlope = param; break;
    case 6: mBiasMix = param; break;
    }
  }

  void Pecto::applyRandomize()
  {
    int target = CLAMP(0, 8, (int)(mXformTarget.value() + 0.5f));
    float depth = CLAMP(0.0f, 1.0f, mXformDepth.value());

    auto rndBias = [&](od::Parameter *p, float mn, float mx) {
      if (p) p->hardSet(randomizeValue(p->value(), mn, mx, depth));
    };
    auto rndBiasInt = [&](od::Parameter *p, float mn, float mx) {
      if (p) p->hardSet(floorf(randomizeValue(p->value(), mn, mx, depth) + 0.5f));
    };

    switch (target)
    {
    case 0: // all
      rndBias(mBiasCombSize, 0.001f, 1.0f);
      rndBias(mBiasFeedback, -0.99f, 0.99f);
      rndBiasInt(mBiasResonatorType, 0.0f, 3.0f);
      rndBiasInt(mBiasDensity, 1.0f, 24.0f);
      rndBiasInt(mBiasPattern, 0.0f, 15.0f);
      rndBiasInt(mBiasSlope, 0.0f, 3.0f);
      rndBias(mBiasMix, 0.0f, 1.0f);
      break;
    case 1: rndBias(mBiasCombSize, 0.001f, 1.0f); break;
    case 2: rndBias(mBiasFeedback, -0.99f, 0.99f); break;
    case 3: rndBiasInt(mBiasResonatorType, 0.0f, 3.0f); break;
    case 4: rndBiasInt(mBiasDensity, 1.0f, 24.0f); break;
    case 5: rndBiasInt(mBiasPattern, 0.0f, 15.0f); break;
    case 6: rndBiasInt(mBiasSlope, 0.0f, 3.0f); break;
    case 7: rndBias(mBiasMix, 0.0f, 1.0f); break;
    case 8: // reset
      if (mBiasCombSize) mBiasCombSize->hardSet(0.1f);
      if (mBiasFeedback) mBiasFeedback->hardSet(0.5f);
      if (mBiasResonatorType) mBiasResonatorType->hardSet(0.0f);
      if (mBiasDensity) mBiasDensity->hardSet(8.0f);
      if (mBiasPattern) mBiasPattern->hardSet(0.0f);
      if (mBiasSlope) mBiasSlope->hardSet(0.0f);
      if (mBiasMix) mBiasMix->hardSet(0.5f);
      break;
    }
  }

  // --- Process ---

  void Pecto::process()
  {
    Internal &s = *mpInternal;

    if (!s.buffer || mMaxDelayInSamples == 0)
    {
      float *in = mIn.buffer();
      float *out = mOut.buffer();
      for (int i = 0; i < FRAMELENGTH; i++)
        out[i] = in[i];
      return;
    }

    float *in = mIn.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();
    int16_t *buf = (int16_t *)s.buffer;
    int maxDelay = mMaxDelayInSamples;
    float sr = globalConfig.sampleRate;

    // Fixed 20 Hz one-pole DC blocker at the output. Pecto is an audio-
    // rate resonator -- no LFO use case to preserve -- so the filter
    // runs unconditionally. Coefficient from (1 - 2pi*fc/sr), which
    // matches exp(-2pi*fc/sr) for small fc/sr.
    const float dcR = 1.0f - 6.28318530718f * 20.0f / sr;

    // CombSize parameter is in seconds
    float combSize = CLAMP(0.00002f, 20.0f, mCombSize.value());
    // Feedback is bipolar: negative values phase-invert each tap loop,
    // yielding a different comb character. |feedback| clamped to 0.99
    // for stability (matches the old positive-only ceiling).
    float feedback = CLAMP(-0.99f, 0.99f, mFeedback.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float inputLevel = CLAMP(0.0f, 4.0f, mInputLevel.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float tanhAmt = CLAMP(0.0f, 1.0f, mTanhAmt.value());
    int density = CLAMP(1, 24, (int)(mDensity.value() + 0.5f));
    // Read pattern/slope/resonator from Bias refs (direct from UI)
    // since tied ParameterAdapters may not be scheduled without graph connections
    int pattern = mBiasPattern ? CLAMP(0, 15, (int)(mBiasPattern->value() + 0.5f))
                               : CLAMP(0, 15, (int)(mPattern.value() + 0.5f));
    int slope = mBiasSlope ? CLAMP(0, 3, (int)(mBiasSlope->value() + 0.5f))
                           : CLAMP(0, 3, (int)(mSlope.value() + 0.5f));
    int resonatorType = mBiasResonatorType ? CLAMP(0, 3, (int)(mBiasResonatorType->value() + 0.5f))
                                           : CLAMP(0, 3, (int)(mResonatorType.value() + 0.5f));

    // V/Oct pitch
    float voctPitch = mVOctPitch.value() * 10.0f;

    // Dirty-check tap distribution
    if (density != mLastDensity || pattern != mLastPattern || slope != mLastSlope)
    {
      recomputeTaps(density, pattern, slope);
    }

    // Compute base delay in samples from comb size, V/Oct shrinks delay (raises pitch)
    float baseDelay = combSize * sr / powf(2.0f, voctPitch);
    if (baseDelay < 1.0f) baseDelay = 1.0f;
    if (baseDelay > (float)(maxDelay - 1)) baseDelay = (float)(maxDelay - 1);

    // Snap-and-fade target update (matches od::Delay::process pattern).
    // Tap positions snap to the new baseDelay only when the previous
    // 25ms crossfade has completed -- that's when mFade.done() is true.
    // While the fade is in progress, mCachedDelaySamples0 (old) and
    // mCachedDelaySamples (new) hold the two endpoints; per-sample
    // crossfade interpolates between them.
    //
    // tapWeight[] tracks the slope envelope which only changes when
    // density / pattern / slope ratchet (handled by recomputeTaps's
    // dirty check). It can update every block without zipper -- only
    // continuous baseDelay motion needs the fade.
    for (int t = 0; t < density; t++)
      mCachedTapWeight[t] = s.tapWeight[t];

    if (mFade.done())
    {
      mFade.reset(1, 0);
      // snap: old <- current "new" target
      for (int t = 0; t < density; t++)
        mCachedDelaySamples0[t] = mCachedDelaySamples[t];
      // recompute new target
      for (int t = 0; t < density; t++)
        mCachedDelaySamples[t] = baseDelay * s.tapPosition[t];
    }

    // Per-sample fade weight buffer (1.0 -> 0.0 across the ramp;
    // matches od::Delay's mFade.getInterpolatedFrame() usage).
    float *fade = od::AudioThread::getFrame();
    mFade.getInterpolatedFrame(fade);

    // Comb feedback: direct, no tap-count normalization
    // (unlike Petrichor, feedback is from a single tap, not a sum)
    float fbNorm = feedback;

    // Xform gate edge detection
    {
      float *xgate = mXformGate.buffer();
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        bool high = xgate[i] > 0.5f;
        if (high && !mXformGateWasHigh)
          applyRandomize();
        mXformGateWasHigh = high;
      }
      if (mManualFire)
      {
        applyRandomize();
        mManualFire = false;
      }
    }

    // No tap normalization -- comb taps sum coherently for resonance.
    // Output limiter (tanh) handles any clipping.

    // Resonator type and sitar-mode are block-constant. Hoist branches
    // out of the per-sample loop: isSitar gates the jawari write-offset
    // path (only path that differs structurally between types).
    const bool isSitar = (resonatorType == 3);

    // Scratch buffers for the 3-pass per-sample tap processing.
    // Separating compute / gather / combine lets the compiler auto-
    // vectorize the arithmetic passes (A and C) on NEON targets;
    // gather (B) stays scalar but is now isolated and prefetched
    // further ahead. idx0/idx1 are int32_t so NEON intrinsic
    // vst1q_s32 compiles (it wants int32_t*, not int*, on ARM GCC).
    //
    // Two scratch sets per pipeline stage: ...0 = OLD positions
    // (snap-fade source), unsuffixed = NEW positions (snap-fade
    // target). Per-sample loop runs the existing pipeline twice
    // (once per set) and crossfades in the final accumulator stage.
    int32_t idx0_0[kMaxCombTaps], idx0[kMaxCombTaps];
    int32_t idx1_0[kMaxCombTaps], idx1[kMaxCombTaps];
    float frac_0[kMaxCombTaps], frac[kMaxCombTaps];
    int16_t sA_0[kMaxCombTaps], sA[kMaxCombTaps];
    int16_t sB_0[kMaxCombTaps], sB[kMaxCombTaps];
    const float scale = 1.0f / 32767.0f;

    // Process audio
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i] * inputLevel;

      // Read taps before writing (so we read previous frame's data)
      if (s.writeIndex >= maxDelay) s.writeIndex = 0;

      float writeIdxF = (float)s.writeIndex;
      float maxDelayF = (float)maxDelay;

      // ============================================================
      // Snap-and-fade dual pipeline (matches od::Delay's two-read
      // crossfade). Per output sample we run pass A + B + C twice:
      // once with mCachedDelaySamples0 (OLD positions) and once with
      // mCachedDelaySamples (NEW positions), then crossfade the
      // two `wet` (and lastTapOut for feedback) by the per-sample
      // fade weight. fade goes 1.0 -> 0.0 over the ramp, so:
      //   wet = w * wet_old + (1 - w) * wet_new
      // matches od::Delay::pushSamples line 202.
      // ============================================================

#ifdef PECTO_HAS_NEON
      // --- Pass A (NEON) for OLD positions ---
      {
        const float32x4_t writeIdxVec = vdupq_n_f32(writeIdxF);
        const float32x4_t zeroVec = vdupq_n_f32(0.0f);
        const float32x4_t maxDelayFVec = vdupq_n_f32(maxDelayF);
        const int32x4_t maxDelayVec = vdupq_n_s32(maxDelay);
        const int32x4_t oneVec = vdupq_n_s32(1);
        const int32x4_t zeroIVec = vdupq_n_s32(0);

        int t = 0;
        for (; t + 4 <= density; t += 4)
        {
          float32x4_t delayV = vld1q_f32(&mCachedDelaySamples0[t]);
          float32x4_t p = vsubq_f32(writeIdxVec, delayV);
          uint32x4_t negMask = vcltq_f32(p, zeroVec);
          float32x4_t pWrap = vaddq_f32(p, maxDelayFVec);
          p = vbslq_f32(negMask, pWrap, p);
          int32x4_t i0v = vcvtq_s32_f32(p);
          int32x4_t i1v = vaddq_s32(i0v, oneVec);
          uint32x4_t wrapMask = vcgeq_s32(i1v, maxDelayVec);
          i1v = vbslq_s32(wrapMask, zeroIVec, i1v);
          float32x4_t fracV = vsubq_f32(p, vcvtq_f32_s32(i0v));
          vst1q_s32(&idx0_0[t], i0v);
          vst1q_s32(&idx1_0[t], i1v);
          vst1q_f32(&frac_0[t], fracV);
        }
        for (; t < density; t++)
        {
          float p = writeIdxF - mCachedDelaySamples0[t];
          if (p < 0.0f) p += maxDelayF;
          int i0 = (int)p;
          int i1 = i0 + 1;
          if (i1 >= maxDelay) i1 = 0;
          idx0_0[t] = i0;
          idx1_0[t] = i1;
          frac_0[t] = p - (float)i0;
        }
      }
      // --- Pass A (NEON) for NEW positions ---
      {
        const float32x4_t writeIdxVec = vdupq_n_f32(writeIdxF);
        const float32x4_t zeroVec = vdupq_n_f32(0.0f);
        const float32x4_t maxDelayFVec = vdupq_n_f32(maxDelayF);
        const int32x4_t maxDelayVec = vdupq_n_s32(maxDelay);
        const int32x4_t oneVec = vdupq_n_s32(1);
        const int32x4_t zeroIVec = vdupq_n_s32(0);

        int t = 0;
        for (; t + 4 <= density; t += 4)
        {
          float32x4_t delayV = vld1q_f32(&mCachedDelaySamples[t]);
          float32x4_t p = vsubq_f32(writeIdxVec, delayV);
          uint32x4_t negMask = vcltq_f32(p, zeroVec);
          float32x4_t pWrap = vaddq_f32(p, maxDelayFVec);
          p = vbslq_f32(negMask, pWrap, p);
          int32x4_t i0v = vcvtq_s32_f32(p);
          int32x4_t i1v = vaddq_s32(i0v, oneVec);
          uint32x4_t wrapMask = vcgeq_s32(i1v, maxDelayVec);
          i1v = vbslq_s32(wrapMask, zeroIVec, i1v);
          float32x4_t fracV = vsubq_f32(p, vcvtq_f32_s32(i0v));
          vst1q_s32(&idx0[t], i0v);
          vst1q_s32(&idx1[t], i1v);
          vst1q_f32(&frac[t], fracV);
        }
        for (; t < density; t++)
        {
          float p = writeIdxF - mCachedDelaySamples[t];
          if (p < 0.0f) p += maxDelayF;
          int i0 = (int)p;
          int i1 = i0 + 1;
          if (i1 >= maxDelay) i1 = 0;
          idx0[t] = i0;
          idx1[t] = i1;
          frac[t] = p - (float)i0;
        }
      }
#else
      // Pass A (scalar): both position sets back-to-back.
      for (int t = 0; t < density; t++)
      {
        float p = writeIdxF - mCachedDelaySamples0[t];
        if (p < 0.0f) p += maxDelayF;
        int i0 = (int)p;
        int i1 = i0 + 1;
        if (i1 >= maxDelay) i1 = 0;
        idx0_0[t] = i0;
        idx1_0[t] = i1;
        frac_0[t] = p - (float)i0;
      }
      for (int t = 0; t < density; t++)
      {
        float p = writeIdxF - mCachedDelaySamples[t];
        if (p < 0.0f) p += maxDelayF;
        int i0 = (int)p;
        int i1 = i0 + 1;
        if (i1 >= maxDelay) i1 = 0;
        idx0[t] = i0;
        idx1[t] = i1;
        frac[t] = p - (float)i0;
      }
#endif

      // Pass B: scalar gather, both sets. Prefetch 8 taps ahead per
      // set. 192 KB buffer misses L1 frequently; the OLD and NEW
      // index arrays may point to nearby buffer regions during the
      // fade so cache hits should improve as the fade progresses.
      for (int t = 0; t < density; t++)
      {
        int pfIdx = t + 8;
        if (pfIdx < density)
        {
          __builtin_prefetch(&buf[idx0_0[pfIdx]], 0, 1);
          __builtin_prefetch(&buf[idx0[pfIdx]], 0, 1);
        }
        sA_0[t] = buf[idx0_0[t]];
        sB_0[t] = buf[idx1_0[t]];
        sA[t]   = buf[idx0[t]];
        sB[t]   = buf[idx1[t]];
      }

      float wet_old = 0.0f, wet_new = 0.0f;
      float lastTapOut_old = 0.0f, lastTapOut_new = 0.0f;

#ifdef PECTO_HAS_NEON
      // --- Pass C (NEON) for OLD ---
      {
        const float32x4_t scaleVec = vdupq_n_f32(scale);
        float32x4_t wetVec = vdupq_n_f32(0.0f);
        int t = 0;
        for (; t + 4 <= density; t += 4)
        {
          int16x4_t sAi = vld1_s16(&sA_0[t]);
          int16x4_t sBi = vld1_s16(&sB_0[t]);
          float32x4_t aV = vmulq_f32(vcvtq_f32_s32(vmovl_s16(sAi)), scaleVec);
          float32x4_t bV = vmulq_f32(vcvtq_f32_s32(vmovl_s16(sBi)), scaleVec);
          float32x4_t fV = vld1q_f32(&frac_0[t]);
          float32x4_t wV = vld1q_f32(&mCachedTapWeight[t]);
          float32x4_t tapV = vmlaq_f32(aV, vsubq_f32(bV, aV), fV);
          tapV = vmulq_f32(tapV, wV);
          wetVec = vaddq_f32(wetVec, tapV);
          lastTapOut_old = vgetq_lane_f32(tapV, 3);
        }
        float32x2_t loHi = vadd_f32(vget_low_f32(wetVec), vget_high_f32(wetVec));
        wet_old = vget_lane_f32(vpadd_f32(loHi, loHi), 0);
        for (; t < density; t++)
        {
          float a = (float)sA_0[t] * scale;
          float b = (float)sB_0[t] * scale;
          float tapOut = (a + (b - a) * frac_0[t]) * mCachedTapWeight[t];
          wet_old += tapOut;
          lastTapOut_old = tapOut;
        }
      }
      // --- Pass C (NEON) for NEW ---
      {
        const float32x4_t scaleVec = vdupq_n_f32(scale);
        float32x4_t wetVec = vdupq_n_f32(0.0f);
        int t = 0;
        for (; t + 4 <= density; t += 4)
        {
          int16x4_t sAi = vld1_s16(&sA[t]);
          int16x4_t sBi = vld1_s16(&sB[t]);
          float32x4_t aV = vmulq_f32(vcvtq_f32_s32(vmovl_s16(sAi)), scaleVec);
          float32x4_t bV = vmulq_f32(vcvtq_f32_s32(vmovl_s16(sBi)), scaleVec);
          float32x4_t fV = vld1q_f32(&frac[t]);
          float32x4_t wV = vld1q_f32(&mCachedTapWeight[t]);
          float32x4_t tapV = vmlaq_f32(aV, vsubq_f32(bV, aV), fV);
          tapV = vmulq_f32(tapV, wV);
          wetVec = vaddq_f32(wetVec, tapV);
          lastTapOut_new = vgetq_lane_f32(tapV, 3);
        }
        float32x2_t loHi = vadd_f32(vget_low_f32(wetVec), vget_high_f32(wetVec));
        wet_new = vget_lane_f32(vpadd_f32(loHi, loHi), 0);
        for (; t < density; t++)
        {
          float a = (float)sA[t] * scale;
          float b = (float)sB[t] * scale;
          float tapOut = (a + (b - a) * frac[t]) * mCachedTapWeight[t];
          wet_new += tapOut;
          lastTapOut_new = tapOut;
        }
      }

#else
      // Pass C (scalar): both position sets back-to-back, then crossfade.
      for (int t = 0; t < density; t++)
      {
        float a = (float)sA_0[t] * scale;
        float b = (float)sB_0[t] * scale;
        float tapOut = (a + (b - a) * frac_0[t]) * mCachedTapWeight[t];
        wet_old += tapOut;
        lastTapOut_old = tapOut;
      }
      for (int t = 0; t < density; t++)
      {
        float a = (float)sA[t] * scale;
        float b = (float)sB[t] * scale;
        float tapOut = (a + (b - a) * frac[t]) * mCachedTapWeight[t];
        wet_new += tapOut;
        lastTapOut_new = tapOut;
      }
#endif

      // Crossfade old <-> new (matches od::Delay pushSamples line 202).
      const float w = fade[i];
      float wet = w * wet_old + (1.0f - w) * wet_new;
      float lastTapOut = w * lastTapOut_old + (1.0f - w) * lastTapOut_new;


      // Feedback from longest tap (last position, highest delay)
      float fb = lastTapOut * fbNorm;

      // Resonator type processing on feedback
      switch (resonatorType)
      {
      case 0: // Raw -- direct wire, bright and metallic
        break;
      case 1: // Guitar -- one-pole LP damping (Karplus-Strong)
      {
        // Coefficient ~0.15 gives strong HF damping: highs decay fast, lows ring
        float coeff = 0.15f;
        s.fbFilterState += (fb - s.fbFilterState) * coeff;
        fb = s.fbFilterState;
        break;
      }
      case 2: // Clarinet -- soft clip + one-pole HP for odd-harmonic emphasis
      {
        // Drive into soft clip to generate odd harmonics
        float driven = fb * 3.0f;
        fb = driven - (driven * driven * driven) / 3.0f;
        fb *= 0.33f; // compensate gain
        break;
      }
      case 3: // Sitar -- amplitude-dependent delay modulation (jawari buzz)
      {
        // Track amplitude envelope of feedback signal
        float absVal = fabsf(fb);
        float attackCoeff = 0.01f;  // fast attack
        float releaseCoeff = 0.001f; // slow release
        if (absVal > s.sitarEnvFollower)
          s.sitarEnvFollower += (absVal - s.sitarEnvFollower) * attackCoeff;
        else
          s.sitarEnvFollower += (absVal - s.sitarEnvFollower) * releaseCoeff;
        break;
      }
      }

      // Write input + feedback combined. Sitar modulates the write
      // position for jawari pitch wobble; other types write straight.
      float fbInjection = (fabsf(fb) > 1.5f) ? fast_tanh(fb) : fb;
      int writePos = s.writeIndex;
      if (isSitar && s.sitarEnvFollower > 0.00025f)
      {
        // Modulate delay by up to ~+/- 2 samples based on amplitude
        int modPos = writePos + (int)(s.sitarEnvFollower * 4.0f);
        if (modPos >= maxDelay) modPos -= maxDelay;
        if (modPos < 0) modPos += maxDelay;
        bufWrite(buf, modPos, x + fbInjection);
      }
      else
      {
        bufWrite(buf, writePos, x + fbInjection);
      }

      // Advance write index
      s.writeIndex++;
      if (s.writeIndex >= maxDelay) s.writeIndex = 0;

      // Mix
      float mixed = x * (1.0f - mix) + wet * mix;

      // User saturation
      if (tanhAmt > 0.001f)
      {
        float drive = 1.0f + tanhAmt * 3.0f;
        mixed = mixed * (1.0f - tanhAmt) + fast_tanh(mixed * drive) * tanhAmt;
      }

      // DC blocker (fixed 20 Hz). Feedback resonance can accumulate DC
      // over time, especially with asymmetric saturation stages.
      float dcOut = mixed - s.dcX1 + dcR * s.dcY1;
      s.dcX1 = mixed;
      s.dcY1 = dcOut;

      // Output
      out[i] = fast_tanh(dcOut * outputLevel);
    }

    // Step the snap-and-fade ramp once per process() call -- matches
    // od::Delay::process line 162. mFade.step() decrements mCount; when
    // it reaches zero, mFade.done() returns true and the next process
    // call will snap to the new target.
    mFade.step();
    od::AudioThread::releaseFrame(fade);
  }

} // namespace stolmine
