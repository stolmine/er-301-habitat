// Larets -- clock-driven stepped multi-effect processor

#include "Larets.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static const int kMaxSteps = 16;
  static const int kBufferSize = 96000;
  static const int kCrossfadeSamples = 64;

  enum FxType
  {
    FX_OFF = 0, FX_STUTTER, FX_REVERSE, FX_BITCRUSH,
    FX_DOWNSAMPLE, FX_FILTER, FX_PITCHSHIFT, FX_TAPESTOP,
    FX_GATE, FX_DISTORTION, FX_SHUFFLE, FX_DELAY, FX_COMB, FX_COUNT
  };

  static uint32_t lRandState = 98765;
  static inline float lRandFloat()
  {
    lRandState = lRandState * 1664525u + 1013904223u;
    return (float)((int32_t)lRandState) / (float)0x7FFFFFFF;
  }

  struct Larets::Internal
  {
    int type[kMaxSteps];
    float param[kMaxSteps];
    int ticks[kMaxSteps];
    float buffer[kBufferSize];
    int writePos;
    float readPos;
    float ic1eq, ic2eq;
    float holdSample;
    int decimCounter;
    float prevOutput;
    int crossfadeCounter;
    float cachedPitchRate;
    float cachedBaseFreq;
    int tmpType[kMaxSteps];
    float tmpParam[kMaxSteps];
    int tmpTicks[kMaxSteps];
    float stepProgress;
    float compDetector;
    float vizRing[128];
    int vizPos;
    int vizDecimCounter;

    void Init()
    {
      memset(buffer, 0, sizeof(buffer));
      for (int i = 0; i < kMaxSteps; i++) { type[i] = FX_OFF; param[i] = 0.5f; ticks[i] = 1; }
      writePos = 0; readPos = 0.0f;
      ic1eq = 0.0f; ic2eq = 0.0f;
      holdSample = 0.0f; decimCounter = 0;
      prevOutput = 0.0f; crossfadeCounter = 0;
      cachedPitchRate = 1.0f; cachedBaseFreq = 40.0f;
      stepProgress = 0.0f;
      compDetector = 0.0f;
      memset(vizRing, 0, sizeof(vizRing));
      vizPos = 0;
      vizDecimCounter = 0;
    }
  };

  Larets::Larets()
  {
    addInput(mIn); addInput(mClock); addInput(mReset); addInput(mTransform);
    addOutput(mOut);
    addParameter(mStepCount); addParameter(mSkew); addParameter(mMix);
    addParameter(mInputLevel); addParameter(mOutputLevel); addParameter(mCompressAmt);
    addParameter(mClockDiv); addParameter(mTransformFunc); addParameter(mTransformDepth);
    addParameter(mLoopLength);
    addParameter(mEditType); addParameter(mEditParam); addParameter(mEditTicks);
    mpInternal = new Internal();
    mpInternal->Init();
    float p0 = mpInternal->param[0];
    mpInternal->cachedPitchRate = powf(2.0f, (p0 * 24.0f - 12.0f) / 12.0f);
    mpInternal->cachedBaseFreq = 40.0f * powf(500.0f, p0);
  }

  Larets::~Larets() { delete mpInternal; }

  int Larets::getStepCount() { return CLAMP(1, kMaxSteps, (int)(mStepCount.value() + 0.5f)); }
  int Larets::getStepType(int i) { return mpInternal->type[CLAMP(0, kMaxSteps - 1, i)]; }
  void Larets::setStepType(int i, int v) { mpInternal->type[CLAMP(0, kMaxSteps - 1, i)] = CLAMP(0, FX_COUNT - 1, v); }
  float Larets::getStepParam(int i) { return mpInternal->param[CLAMP(0, kMaxSteps - 1, i)]; }
  void Larets::setStepParam(int i, float v) { mpInternal->param[CLAMP(0, kMaxSteps - 1, i)] = CLAMP(0.0f, 1.0f, v); }
  int Larets::getStepTicks(int i) { return mpInternal->ticks[CLAMP(0, kMaxSteps - 1, i)]; }
  void Larets::setStepTicks(int i, int v) { mpInternal->ticks[CLAMP(0, kMaxSteps - 1, i)] = MAX(1, v); }

  void Larets::loadStep(int i)
  {
    i = CLAMP(0, kMaxSteps - 1, i);
    mEditType.hardSet((float)mpInternal->type[i]);
    mEditParam.hardSet(mpInternal->param[i]);
    mEditTicks.hardSet((float)mpInternal->ticks[i]);
  }

  void Larets::storeStep(int i)
  {
    i = CLAMP(0, kMaxSteps - 1, i);
    mpInternal->type[i] = CLAMP(0, FX_COUNT - 1, (int)(mEditType.value() + 0.5f));
    mpInternal->param[i] = CLAMP(0.0f, 1.0f, mEditParam.value());
    mpInternal->ticks[i] = MAX(1, (int)(mEditTicks.value() + 0.5f));
  }

  void Larets::fireTransform() { mManualFire = true; }

  static float skewMultiplier(int step, int stepCount, float skew)
  {
    if (stepCount <= 1) return 1.0f;
    float pos = (float)step / (float)(stepCount - 1);
    return fmaxf(0.25f, 1.0f + skew * (pos - 0.5f) * 2.0f);
  }

  float Larets::getEffectiveTickCount(int i)
  {
    int sc = getStepCount();
    float skew = CLAMP(-1.0f, 1.0f, mSkew.value());
    return roundf((float)mpInternal->ticks[CLAMP(0, kMaxSteps - 1, i)] * skewMultiplier(i, sc, skew));
  }

  float Larets::getOutputSample(int idx)
  {
    return mpInternal->vizRing[(mpInternal->vizPos + idx) & 127];
  }

  float Larets::getClockPeriodSeconds()
  {
    if (mClockPeriodSamples <= 0) return 1.0f;
    return (float)mClockPeriodSamples / globalConfig.sampleRate;
  }

  void Larets::applyTransform()
  {
    Internal &s = *mpInternal;
    int func = CLAMP(0, 6, (int)(mTransformFunc.value() + 0.5f));
    float depth = CLAMP(0.0f, 1.0f, mTransformDepth.value());
    int sc = getStepCount();

    switch (func)
    {
    case 0: // Randomize all (type + param + ticks)
      for (int i = 0; i < sc; i++)
        if ((lRandFloat() + 1.0f) * 0.5f < depth)
        {
          s.type[i] = (int)((lRandFloat() + 1.0f) * 0.5f * (FX_COUNT - 1) + 0.5f) % FX_COUNT;
          s.param[i] = CLAMP(0.0f, 1.0f, (lRandFloat() + 1.0f) * 0.5f);
          s.ticks[i] = 1 + (int)((lRandFloat() + 1.0f) * 0.5f * 3.0f);
        }
      break;
    case 1: // Randomize type + params
      for (int i = 0; i < sc; i++)
        if ((lRandFloat() + 1.0f) * 0.5f < depth)
        {
          s.type[i] = (int)((lRandFloat() + 1.0f) * 0.5f * (FX_COUNT - 1) + 0.5f) % FX_COUNT;
          s.param[i] = CLAMP(0.0f, 1.0f, (lRandFloat() + 1.0f) * 0.5f);
        }
      break;
    case 2: // Randomize types only
      for (int i = 0; i < sc; i++)
        if ((lRandFloat() + 1.0f) * 0.5f < depth)
          s.type[i] = (int)((lRandFloat() + 1.0f) * 0.5f * (FX_COUNT - 1) + 0.5f) % FX_COUNT;
      break;
    case 3: // Randomize params only
      for (int i = 0; i < sc; i++)
        if ((lRandFloat() + 1.0f) * 0.5f < depth)
          s.param[i] = CLAMP(0.0f, 1.0f, (lRandFloat() + 1.0f) * 0.5f);
      break;
    case 4: // Randomize ticks
      for (int i = 0; i < sc; i++)
        if ((lRandFloat() + 1.0f) * 0.5f < depth)
          s.ticks[i] = 1 + (int)((lRandFloat() + 1.0f) * 0.5f * 3.0f);
      break;
    case 5: // Rotate
    {
      int rot = MAX(1, (int)(depth * (float)sc)) % sc;
      for (int i = 0; i < sc; i++) { int src = (i + rot) % sc; s.tmpType[i] = s.type[src]; s.tmpParam[i] = s.param[src]; s.tmpTicks[i] = s.ticks[src]; }
      memcpy(s.type, s.tmpType, sc * sizeof(int));
      memcpy(s.param, s.tmpParam, sc * sizeof(float));
      memcpy(s.ticks, s.tmpTicks, sc * sizeof(int));
      break;
    }
    case 6: // Reverse
      for (int i = 0; i < sc / 2; i++)
      {
        int j = sc - 1 - i;
        int t = s.type[i]; s.type[i] = s.type[j]; s.type[j] = t;
        float p = s.param[i]; s.param[i] = s.param[j]; s.param[j] = p;
        int k = s.ticks[i]; s.ticks[i] = s.ticks[j]; s.ticks[j] = k;
      }
      break;
    }
  }

  float Larets::processEffect(float input, int type, float param, float sp)
  {
    Internal &s = *mpInternal;
    float sr = globalConfig.sampleRate;

    switch (type)
    {
    case FX_OFF: return input;

    case FX_STUTTER:
    {
      int len = MAX(1, (int)(param * 8192.0f));
      int idx = (((s.writePos - len) + kBufferSize) % kBufferSize + (int)s.readPos) % kBufferSize;
      float o = s.buffer[idx];
      s.readPos += 1.0f; if ((int)s.readPos >= len) s.readPos = 0.0f;
      return o;
    }

    case FX_REVERSE:
    {
      int len = MAX(1, (int)(sr * 0.25f));
      int head = ((s.writePos - len) + kBufferSize) % kBufferSize;
      float o = s.buffer[(head + len - 1 - (int)s.readPos + kBufferSize) % kBufferSize];
      float rate = 0.5f + param * 1.5f;
      s.readPos += rate; if ((int)s.readPos >= len) s.readPos = 0.0f;
      return o;
    }

    case FX_BITCRUSH:
    {
      float lvl = powf(2.0f, 1.0f + param * 15.0f);
      return floorf(input * lvl + 0.5f) / lvl;
    }

    case FX_DOWNSAMPLE:
    {
      int factor = 1 + (int)(param * 31.0f);
      if (++s.decimCounter >= factor) { s.holdSample = input; s.decimCounter = 0; }
      return s.holdSample;
    }

    case FX_FILTER:
    {
      float freq = CLAMP(20.0f, sr * 0.49f, s.cachedBaseFreq * (1.0f + sp * 4.0f));
      float g = tanf(3.14159f * freq / sr), k = 1.05f;
      float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
      float v3 = input - s.ic2eq, v1 = a1 * s.ic1eq + a2 * v3, v2 = s.ic2eq + a2 * s.ic1eq + a3 * v3;
      s.ic1eq = 2.0f * v1 - s.ic1eq; s.ic2eq = 2.0f * v2 - s.ic2eq;
      return v2;
    }

    case FX_PITCHSHIFT:
    {
      float rate = s.cachedPitchRate;
      int len = MAX(1, (int)(sr * 0.1f));
      int head = ((s.writePos - len) + kBufferSize) % kBufferSize;
      float o = s.buffer[(head + (int)s.readPos) % kBufferSize];
      s.readPos += rate; if ((int)s.readPos >= len) s.readPos -= (float)len;
      return o;
    }

    case FX_TAPESTOP:
    {
      float rate = fmaxf(0.0f, 1.0f - sp * (0.5f + param * 0.5f));
      int len = MAX(1, (int)(sr * 0.5f));
      int head = ((s.writePos - len) + kBufferSize) % kBufferSize;
      float o = s.buffer[(head + (int)s.readPos) % kBufferSize];
      s.readPos += rate; if ((int)s.readPos >= len) s.readPos = (float)(len - 1);
      return o;
    }

    case FX_GATE:
    {
      float env = (param < 0.5f) ? ((sp < 0.5f) ? 1.0f : 0.0f) : fmaxf(0.0f, 1.0f - sp * 2.0f * (param + 0.5f));
      return input * env;
    }

    case FX_DISTORTION:
      return tanhf(input * (1.0f + param * 9.0f));

    case FX_SHUFFLE:
    {
      int segs = 2 + (int)(param * 6.0f);
      int len = MAX(1, (int)(sr * 0.25f));
      int segLen = MAX(1, len / segs);
      int head = ((s.writePos - len) + kBufferSize) % kBufferSize;
      uint32_t h = (uint32_t)(mStep * 2654435761u) ^ (uint32_t)(segs * 2246822519u);
      int offset = (int)((h >> 16) % (uint32_t)segs) * segLen;
      float o = s.buffer[(head + offset + (int)s.readPos % segLen) % kBufferSize];
      s.readPos += 1.0f; if ((int)s.readPos >= len) s.readPos = 0.0f;
      return o;
    }

    case FX_DELAY:
    {
      int delaySamples = MAX(1, (int)(param * sr * 0.5f));
      int idx = ((s.writePos - delaySamples) + kBufferSize) % kBufferSize;
      return s.buffer[idx];
    }

    case FX_COMB:
    {
      int delaySamples = MAX(1, (int)(20.0f + param * (sr * 0.02f - 20.0f)));
      int idx = ((s.writePos - delaySamples) + kBufferSize) % kBufferSize;
      return input + s.buffer[idx] * 0.7f;
    }

    default: return input;
    }
  }

  void Larets::process()
  {
    Internal &s = *mpInternal;
    float *in = mIn.buffer(), *clock = mClock.buffer();
    float *reset = mReset.buffer(), *xform = mTransform.buffer(), *out = mOut.buffer();
    float sr = globalConfig.sampleRate;

    int stepCount = CLAMP(1, kMaxSteps, (int)(mStepCount.value() + 0.5f));
    float skew = CLAMP(-1.0f, 1.0f, mSkew.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float inputLevel = CLAMP(0.0f, 4.0f, mInputLevel.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float compAmt = CLAMP(0.0f, 1.0f, mCompressAmt.value());
    float compAttack = 1.0f - expf(-1.0f / (0.001f * sr));
    float compRelease = 1.0f - expf(-1.0f / (0.1f * sr));
    float compThreshold = 1.0f - compAmt * 0.8f;
    float compThreshDb = 10.0f * log10f(compThreshold * compThreshold + 1e-20f);
    float compRatioFactor = 1.0f - 1.0f / (1.0f + compAmt * 7.0f);
    int clockDiv = MAX(1, (int)(mClockDiv.value() + 0.5f));
    int loopLen = CLAMP(0, stepCount, (int)(mLoopLength.value() + 0.5f));
    int wrapLen = (loopLen > 0) ? loopLen : stepCount;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool xHigh = xform[i] > 0.0f;
      if ((xHigh && !mTransformWasHigh) || mManualFire) { applyTransform(); mManualFire = false; }
      mTransformWasHigh = xHigh;
    }

    int effTicks = MAX(1, (int)roundf((float)s.ticks[mStep % stepCount] * skewMultiplier(mStep % stepCount, stepCount, skew)));

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float inputSample = in[i] * inputLevel;
      s.buffer[s.writePos] = inputSample;
      s.writePos = (s.writePos + 1) % kBufferSize;

      bool clockHigh = clock[i] > 0.5f, resetHigh = reset[i] > 0.5f;
      bool clockRise = clockHigh && !mClockWasHigh, resetRise = resetHigh && !mResetWasHigh;
      mClockWasHigh = clockHigh; mResetWasHigh = resetHigh;
      mSamplesSinceLastClock++;
      if (clockRise) { mClockPeriodSamples = mSamplesSinceLastClock; mSamplesSinceLastClock = 0; }

      if (resetRise)
      {
        mStep = 0; mTickCount = 0; mDivCount = 0;
        effTicks = MAX(1, (int)roundf((float)s.ticks[0] * skewMultiplier(0, stepCount, skew)));
      }

      if (clockRise)
      {
        if (++mDivCount >= clockDiv)
        {
          mDivCount = 0;
          if (++mTickCount >= effTicks)
          {
            s.crossfadeCounter = kCrossfadeSamples;
            mStep = (mStep + 1) % wrapLen;
            mTickCount = 0; s.readPos = 0.0f; s.ic1eq = 0.0f; s.ic2eq = 0.0f; s.stepProgress = 0.0f;
            effTicks = MAX(1, (int)roundf((float)s.ticks[mStep] * skewMultiplier(mStep, stepCount, skew)));
            float np = s.param[mStep];
            s.cachedPitchRate = powf(2.0f, (np * 24.0f - 12.0f) / 12.0f);
            s.cachedBaseFreq = 40.0f * powf(500.0f, np);
          }
        }
      }

      s.stepProgress = (effTicks > 0) ? CLAMP(0.0f, 1.0f, (float)mTickCount / (float)effTicks) : 0.0f;

      float wet = processEffect(inputSample, s.type[mStep % stepCount], s.param[mStep % stepCount], s.stepProgress);

      if (s.crossfadeCounter > 0)
      {
        float blend = (float)s.crossfadeCounter / (float)kCrossfadeSamples;
        wet = s.prevOutput * blend + wet * (1.0f - blend);
        s.crossfadeCounter--;
      }
      else
      {
        s.prevOutput = wet;
      }

      float mixed = in[i] * (1.0f - mix) + wet * mix;

      if (compAmt > 0.001f)
      {
        float energy = mixed * mixed;
        if (energy > s.compDetector)
          s.compDetector += (energy - s.compDetector) * compAttack;
        else
          s.compDetector += (energy - s.compDetector) * compRelease;
        float levelDb = 4.3429f * logf(s.compDetector + 1e-20f);
        float overDb = levelDb - compThreshDb;
        if (overDb > 0.0f)
          mixed *= expf(-0.1151f * overDb * compRatioFactor);
      }

      out[i] = mixed * outputLevel;

      if (++s.vizDecimCounter >= 8)
      {
        s.vizDecimCounter = 0;
        s.vizRing[s.vizPos] = out[i];
        s.vizPos = (s.vizPos + 1) & 127;
      }
    }
  }

} // namespace stolmine
