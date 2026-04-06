#include "Pecto.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>
#include <new>
#include <stdlib.h>

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
    return bufRead(buf, idx0) + (bufRead(buf, idx1) - bufRead(buf, idx0)) * frac;
  }

  struct Pecto::Internal
  {
    char *buffer = 0;
    int writeIndex = 0;
    float tapPosition[kMaxCombTaps];
    float tapWeight[kMaxCombTaps];
    float fbFilterState = 0.0f;

    void Init()
    {
      writeIndex = 0;
      fbFilterState = 0.0f;
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
    addOutput(mOutR);
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
    for (int i = 0; i < kMaxCombTaps; i++)
      mCachedTapWeight[i] = 1.0f;
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

  void Pecto::setMono(bool mono) { mMono = mono; }

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

  void Pecto::recomputeTaps(int density, int pattern, int slope)
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

    mLastDensity = density;
    mLastPattern = pattern;
    mLastSlope = slope;
  }

  // --- Xform randomization ---

  static float randomizeValue(float cur, float mn, float mx, float depth)
  {
    float range = mx - mn;
    float r = ((float)(rand() & 0x7FFF) / 16383.0f) * 2.0f - 1.0f;
    float v = cur + r * depth * range * 0.5f;
    return CLAMP(mn, mx, v);
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
      rndBias(mBiasFeedback, 0.0f, 0.99f);
      rndBiasInt(mBiasResonatorType, 0.0f, 3.0f);
      rndBiasInt(mBiasDensity, 1.0f, 64.0f);
      rndBiasInt(mBiasPattern, 0.0f, 15.0f);
      rndBiasInt(mBiasSlope, 0.0f, 3.0f);
      rndBias(mBiasMix, 0.0f, 1.0f);
      break;
    case 1: rndBias(mBiasCombSize, 0.001f, 1.0f); break;
    case 2: rndBias(mBiasFeedback, 0.0f, 0.99f); break;
    case 3: rndBiasInt(mBiasResonatorType, 0.0f, 3.0f); break;
    case 4: rndBiasInt(mBiasDensity, 1.0f, 64.0f); break;
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
      float *outR = mOutR.buffer();
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        out[i] = in[i];
        outR[i] = in[i];
      }
      return;
    }

    float *in = mIn.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();
    float *outR = mOutR.buffer();
    int16_t *buf = (int16_t *)s.buffer;
    int maxDelay = mMaxDelayInSamples;
    float sr = globalConfig.sampleRate;

    // CombSize parameter is in seconds
    float combSize = CLAMP(0.00002f, 20.0f, mCombSize.value());
    float feedback = CLAMP(0.0f, 0.99f, mFeedback.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float inputLevel = CLAMP(0.0f, 4.0f, mInputLevel.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float tanhAmt = CLAMP(0.0f, 1.0f, mTanhAmt.value());
    int density = CLAMP(1, kMaxCombTaps, (int)(mDensity.value() + 0.5f));
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

    // Cache per-tap delay positions
    for (int t = 0; t < density; t++)
    {
      mCachedDelaySamples[t] = baseDelay * s.tapPosition[t];
      mCachedTapWeight[t] = s.tapWeight[t];
    }

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

    // Process audio
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i] * inputLevel;

      // Read taps before writing (so we read previous frame's data)
      if (s.writeIndex >= maxDelay) s.writeIndex = 0;

      float wet = 0.0f;
      float lastTapOut = 0.0f;
      for (int t = 0; t < density; t++)
      {
        float readPos = (float)s.writeIndex - mCachedDelaySamples[t];
        if (readPos < 0.0f) readPos += (float)maxDelay;
        float tapOut = bufReadInterp(buf, readPos, maxDelay) * mCachedTapWeight[t];
        wet += tapOut;
        lastTapOut = tapOut;
      }


      // Feedback from last (longest) tap
      float fb = lastTapOut * fbNorm;

      // Resonator type processing on feedback
      switch (resonatorType)
      {
      case 0: // Raw -- direct
        break;
      case 1: // Guitar -- one-pole LP damping (Karplus-Strong)
        s.fbFilterState += (fb - s.fbFilterState) * 0.4f;
        fb = s.fbFilterState;
        break;
      case 2: // Clarinet -- odd-harmonic nonlinearity
        fb = fb - (fb * fb * fb) / 3.0f;
        break;
      case 3: // Sitar -- amplitude-dependent delay mod (stub)
        break;
      }

      // Write input + feedback combined (avoids int16 clipping on separate writes)
      float fbInjection = (fabsf(fb) > 1.5f) ? fast_tanh(fb) : fb;
      bufWrite(buf, s.writeIndex, x + fbInjection);

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

      // Output
      if (mMono)
      {
        out[i] = fast_tanh(mixed * outputLevel);
      }
      else
      {
        out[i] = fast_tanh(mixed * outputLevel);
        outR[i] = out[i];
      }
    }
  }

} // namespace stolmine
