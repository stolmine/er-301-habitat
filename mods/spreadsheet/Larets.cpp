// Larets -- clock-driven stepped multi-effect processor

#include "Larets.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  // IEEE 754 fast log2/exp2 (from Parfait/Impasto)
  static inline float fast_log2(float x)
  {
    union { float f; int32_t i; } v;
    v.f = x;
    float y = (float)(v.i);
    y *= 1.0f / (1 << 23);
    y -= 127.0f;
    return y;
  }

  static inline float fast_exp2(float x)
  {
    union { float f; int32_t i; } v;
    v.i = (int32_t)((x + 127.0f) * (1 << 23));
    return v.f;
  }

  static inline float fast_log10(float x) { return fast_log2(x) * 0.30103f; }
  static inline float fast_fromDb(float db) { return fast_exp2(db * 0.16609640474f); }

  static const int kMaxSteps = 16;
  static const int kBufferSize = 96000;
  static const int kCrossfadeSamples = 64;

  enum FxType
  {
    FX_OFF = 0, FX_STUTTER, FX_REVERSE, FX_BITCRUSH,
    FX_DOWNSAMPLE, FX_FILTER, FX_PITCHSHIFT,
    FX_DISTORTION, FX_SHUFFLE, FX_DELAY, FX_COMB, FX_COUNT
  };

  static uint32_t lRandState = 98765;
  static inline float lRandFloat()
  {
    lRandState = lRandState * 1664525u + 1013904223u;
    return (float)((int32_t)lRandState) / (float)0x7FFFFFFF;
  }

  struct Larets::Internal
  {
    // shared sequencer + step program
    int type[kMaxSteps];
    float param[kMaxSteps];
    int ticks[kMaxSteps];
    int tmpType[kMaxSteps];
    float tmpParam[kMaxSteps];
    int tmpTicks[kMaxSteps];
    float stepProgress;
    float compDetector;      // linked envelope (max-of-L,R)
    int stepStartPos;        // sequencer-driven; shared index value
    int shuffleOffset;       // shared per-beat random pick
    float vizRing[128];      // viz tap (L+R mix)
    int vizPos;
    int vizDecimCounter;

    // per-channel audio state
    struct ChannelState
    {
      float buffer[kBufferSize];
      int writePos;
      float readPos;
      float ic1eq, ic2eq;
      float holdSample;
      int decimCounter;
      float prevOutput;
      int crossfadeCounter;
      float pitchPhase;
    } ch[2];

    void Init()
    {
      for (int i = 0; i < kMaxSteps; i++) { type[i] = FX_OFF; param[i] = 0.5f; ticks[i] = 1; }
      stepProgress = 0.0f;
      compDetector = 0.0f;
      stepStartPos = 0;
      shuffleOffset = 0;
      memset(vizRing, 0, sizeof(vizRing));
      vizPos = 0;
      vizDecimCounter = 0;
      for (int c = 0; c < 2; c++)
      {
        memset(ch[c].buffer, 0, sizeof(ch[c].buffer));
        ch[c].writePos = 0;
        ch[c].readPos = 0.0f;
        ch[c].ic1eq = 0.0f;
        ch[c].ic2eq = 0.0f;
        ch[c].holdSample = 0.0f;
        ch[c].decimCounter = 0;
        ch[c].prevOutput = 0.0f;
        ch[c].crossfadeCounter = 0;
        ch[c].pitchPhase = 0.0f;
      }
    }
  };

  Larets::Larets()
  {
    addInput(mInL); addInput(mInR);
    addInput(mClock); addInput(mReset); addInput(mTransform);
    addOutput(mOutL); addOutput(mOutR);
    addParameter(mStepCount); addParameter(mSkew); addParameter(mMix);
    addParameter(mOutputLevel); addParameter(mCompressAmt);
    addParameter(mClockDiv); addParameter(mTransformFunc); addParameter(mTransformDepth);
    addParameter(mLoopLength);
    addParameter(mParamOffset);
    addParameter(mEditType); addParameter(mEditParam); addParameter(mEditTicks);
    addOption(mAutoMakeup);
    mAutoMakeup.enableSerialization();
    mpInternal = new Internal();
    mpInternal->Init();
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

  bool Larets::isAutoMakeupEnabled() { return mAutoMakeup.value() == 1; }
  void Larets::toggleAutoMakeup() { mAutoMakeup.set(isAutoMakeupEnabled() ? 2 : 1); }

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

  float Larets::processEffect(float input, Internal &s, int channel, int type, float param, float sp)
  {
    Internal::ChannelState &cs = s.ch[channel];
    float sr = globalConfig.sampleRate;

    switch (type)
    {
    case FX_OFF: return input;

    case FX_STUTTER:
    {
      // Beat-repeat: snapshot the last <div> of a clock period at step start
      // and loop it for the duration of the step. Musical fractions only.
      int period = (mClockPeriodSamples > 0) ? mClockPeriodSamples : (int)(sr * 0.5f);
      static const float kDivs[] = { 1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f, 1.0f };
      int divIdx = CLAMP(0, 4, (int)(param * 4.999f));
      int len = MAX(64, (int)(period * kDivs[divIdx]));
      if (len > kBufferSize / 2) len = kBufferSize / 2;
      int base = ((s.stepStartPos - len) + kBufferSize) % kBufferSize;
      float o = cs.buffer[(base + (int)cs.readPos) % kBufferSize];
      cs.readPos += 1.0f; if ((int)cs.readPos >= len) cs.readPos = 0.0f;
      return o;
    }

    case FX_REVERSE:
    {
      int period = (mClockPeriodSamples > 0) ? mClockPeriodSamples : (int)(sr * 0.5f);
      int len = MAX(1, period);
      if (len > kBufferSize / 2) len = kBufferSize / 2;
      int head = ((cs.writePos - len) + kBufferSize) % kBufferSize;
      float o = cs.buffer[(head + len - 1 - (int)cs.readPos + kBufferSize) % kBufferSize];
      float rate = 0.5f + param * 1.5f;
      cs.readPos += rate; if ((int)cs.readPos >= len) cs.readPos = 0.0f;
      return o;
    }

    case FX_BITCRUSH:
    {
      // param=0 -> 12-bit (clean), param=1 -> ~2.5-bit (heavily crushed).
      // Direction inverted: higher param = more reduction. Matches the viz.
      float lvl = powf(2.0f, 12.0f - param * 9.5f);
      return floorf(input * lvl + 0.5f) / lvl;
    }

    case FX_DOWNSAMPLE:
    {
      int factor = 1 + (int)(param * 31.0f);
      if (++cs.decimCounter >= factor) { cs.holdSample = input; cs.decimCounter = 0; }
      return cs.holdSample;
    }

    case FX_FILTER:
    {
      // Recompute from effective param each sample so the global offset
      // tracks CV smoothly, rather than freezing at step-boundary cache.
      float baseFreq = 40.0f * powf(500.0f, param);
      float freq = CLAMP(20.0f, sr * 0.49f, baseFreq * (1.0f + sp * 4.0f));
      float g = tanf(3.14159f * freq / sr), k = 1.05f;
      float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
      float v3 = input - cs.ic2eq, v1 = a1 * cs.ic1eq + a2 * v3, v2 = cs.ic2eq + a2 * cs.ic1eq + a3 * v3;
      cs.ic1eq = 2.0f * v1 - cs.ic1eq; cs.ic2eq = 2.0f * v2 - cs.ic2eq;
      return v2;
    }

    case FX_PITCHSHIFT:
    {
      // Two-grain overlap pitch shifter (Dattorro-style): two delay reads
      // move through a grain window 180 deg out of phase, windowed with
      // sin^2 so their sum is constant-amplitude. Adapted from the SDK's
      // MonoGrainDelay idea with period tracking live buffer writes.
      float rate = powf(2.0f, (param * 24.0f - 12.0f) / 12.0f);
      int period = (mClockPeriodSamples > 0) ? mClockPeriodSamples : (int)(sr * 0.1f);
      int D = MAX(1024, period / 4);
      if (D > kBufferSize / 2) D = kBufferSize / 2;

      cs.pitchPhase += (1.0f - rate) / (float)D;
      if (cs.pitchPhase >= 1.0f) cs.pitchPhase -= 1.0f;
      else if (cs.pitchPhase < 0.0f) cs.pitchPhase += 1.0f;

      float phA = cs.pitchPhase;
      float phB = phA + 0.5f; if (phB >= 1.0f) phB -= 1.0f;

      int dA = (int)(phA * (float)D);
      int dB = (int)(phB * (float)D);
      int posA = (cs.writePos - dA - 1 + 2 * kBufferSize) % kBufferSize;
      int posB = (cs.writePos - dB - 1 + 2 * kBufferSize) % kBufferSize;

      float wA = sinf(3.14159265f * phA); wA *= wA;
      float wB = sinf(3.14159265f * phB); wB *= wB;

      return cs.buffer[posA] * wA + cs.buffer[posB] * wB;
    }

    case FX_DISTORTION:
    {
      // Hard clip against a unit-ceiling limiter: linear slope up to
      // +/-1, flat after. Drive 1..20 so anything above ~0.05 input
      // clips at max -- plenty of squared-off character. Makeup
      // 1/drive^0.7 leaves a bit of boost in the output relative to a
      // flat 1/drive, so the distortion reads louder than bare input.
      float drive = 1.0f + param * 19.0f;
      float y = input * drive;
      if (y > 1.0f) y = 1.0f;
      else if (y < -1.0f) y = -1.0f;
      return y * powf(drive, -0.7f);
    }

    case FX_SHUFFLE:
    {
      // Beat-repeat with shared per-beat random source pick: loop length
      // is a musical fraction of the clock (same as stutter). The
      // random offset (s.shuffleOffset) is picked once per loop wrap in
      // the sequencer-advance section so L and R play matching slices.
      int period = (mClockPeriodSamples > 0) ? mClockPeriodSamples : (int)(sr * 0.5f);
      static const float kDivs[] = { 1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f, 1.0f };
      int divIdx = CLAMP(0, 4, (int)(param * 4.999f));
      int len = MAX(64, (int)(period * kDivs[divIdx]));
      if (len > kBufferSize / 2) len = kBufferSize / 2;

      int windowLen = period * 2;
      if (len * 4 > windowLen) windowLen = len * 4;
      if (windowLen < len) windowLen = len;
      if (windowLen > kBufferSize / 2) windowLen = kBufferSize / 2;

      int base = ((s.stepStartPos - windowLen) + kBufferSize) % kBufferSize;
      float o = cs.buffer[(base + s.shuffleOffset + (int)cs.readPos) % kBufferSize];
      cs.readPos += 1.0f;
      if ((int)cs.readPos >= len) cs.readPos = 0.0f;
      return o;
    }

    case FX_DELAY:
    {
      int delaySamples = MAX(1, (int)(param * sr * 0.5f));
      int idx = ((cs.writePos - delaySamples) + kBufferSize) % kBufferSize;
      return cs.buffer[idx];
    }

    case FX_COMB:
    {
      int delaySamples = MAX(1, (int)(20.0f + param * (sr * 0.02f - 20.0f)));
      int idx = ((cs.writePos - delaySamples) + kBufferSize) % kBufferSize;
      return input + cs.buffer[idx] * 0.7f;
    }

    default: return input;
    }
  }

  void Larets::process()
  {
    Internal &s = *mpInternal;
    float *inL = mInL.buffer(), *inR = mInR.buffer();
    float *outL = mOutL.buffer(), *outR = mOutR.buffer();
    float *clock = mClock.buffer();
    float *reset = mReset.buffer(), *xform = mTransform.buffer();
    float sr = globalConfig.sampleRate;

    int stepCount = CLAMP(1, kMaxSteps, (int)(mStepCount.value() + 0.5f));
    float skew = CLAMP(-1.0f, 1.0f, mSkew.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float paramOffset = CLAMP(-1.0f, 1.0f, mParamOffset.value());
    float compAmt = CLAMP(0.0f, 1.0f, mCompressAmt.value());
    bool autoMakeup = mAutoMakeup.value() == 1;
    bool compActive = compAmt > 0.001f;

    // CPR single-band: 0 -> no-op, 1 -> aggressive limiter
    // threshold -40 dB, ratio 20:1, 1 ms attack -- enough to clamp peaks
    float compThresholdDb = -compAmt * 40.0f;
    float compRatioI = 1.0f / (1.0f + compAmt * 19.0f);
    float compAttackSec = 0.010f - compAmt * 0.009f;
    float compReleaseSec = 0.200f;
    float compRiseCoeff = expf(-1.0f / (compAttackSec * sr));
    float compFallCoeff = expf(-1.0f / (compReleaseSec * sr));
    float compMakeupGain = autoMakeup
        ? fast_fromDb(-compThresholdDb * (1.0f - compRatioI))
        : 1.0f;

    int clockDiv = MAX(1, (int)(mClockDiv.value() + 0.5f));
    int loopLen = CLAMP(1, kMaxSteps, (int)(mLoopLength.value() + 0.5f));
    int wrapLen = MIN(loopLen, stepCount);

    if (mStep >= stepCount)
      mStep = mStep % stepCount;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool xHigh = xform[i] > 0.0f;
      if ((xHigh && !mTransformWasHigh) || mManualFire) { applyTransform(); mManualFire = false; }
      mTransformWasHigh = xHigh;
    }

    int effTicks = MAX(1, (int)roundf((float)s.ticks[mStep % stepCount] * skewMultiplier(mStep % stepCount, stepCount, skew)));

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Per-channel buffer writes
      s.ch[0].buffer[s.ch[0].writePos] = inL[i];
      s.ch[0].writePos = (s.ch[0].writePos + 1) % kBufferSize;
      s.ch[1].buffer[s.ch[1].writePos] = inR[i];
      s.ch[1].writePos = (s.ch[1].writePos + 1) % kBufferSize;

      bool clockHigh = clock[i] > 0.5f, resetHigh = reset[i] > 0.5f;
      bool clockRise = clockHigh && !mClockWasHigh, resetRise = resetHigh && !mResetWasHigh;
      mClockWasHigh = clockHigh; mResetWasHigh = resetHigh;
      mSamplesSinceLastClock++;
      if (clockRise) { mClockPeriodSamples = mSamplesSinceLastClock; mSamplesSinceLastClock = 0; }

      if (resetRise)
      {
        mStep = 0; mTickCount = 0; mDivCount = 0;
        s.stepStartPos = s.ch[0].writePos;
        effTicks = MAX(1, (int)roundf((float)s.ticks[0] * skewMultiplier(0, stepCount, skew)));
      }

      if (clockRise)
      {
        if (++mDivCount >= clockDiv)
        {
          mDivCount = 0;
          if (++mTickCount >= effTicks)
          {
            mStep = (mStep + 1) % wrapLen;
            mTickCount = 0;
            s.stepProgress = 0.0f;
            s.stepStartPos = s.ch[0].writePos;
            for (int c = 0; c < 2; c++)
            {
              s.ch[c].crossfadeCounter = kCrossfadeSamples;
              s.ch[c].readPos = 0.0f;
              s.ch[c].ic1eq = 0.0f;
              s.ch[c].ic2eq = 0.0f;
            }
            effTicks = MAX(1, (int)roundf((float)s.ticks[mStep] * skewMultiplier(mStep, stepCount, skew)));
          }
        }
      }

      s.stepProgress = (effTicks > 0) ? CLAMP(0.0f, 1.0f, (float)mTickCount / (float)effTicks) : 0.0f;

      int effType = s.type[mStep % stepCount];
      float effParam = s.param[mStep % stepCount] + paramOffset;
      if (effParam < 0.0f) effParam = 0.0f;
      if (effParam > 1.0f) effParam = 1.0f;

      // Shared per-beat shuffle random pick. L and R run the FX_SHUFFLE
      // wrap math identically, so ch[0].readPos == 0 is a reliable beat
      // boundary for both channels. Picking once here keeps L and R
      // playing the same buffer slice rather than diverging.
      if (effType == FX_SHUFFLE && (int)s.ch[0].readPos == 0)
      {
        int period = (mClockPeriodSamples > 0) ? mClockPeriodSamples : (int)(sr * 0.5f);
        static const float kDivs[] = { 1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f, 1.0f };
        int divIdx = CLAMP(0, 4, (int)(effParam * 4.999f));
        int len = MAX(64, (int)(period * kDivs[divIdx]));
        if (len > kBufferSize / 2) len = kBufferSize / 2;
        int windowLen = period * 2;
        if (len * 4 > windowLen) windowLen = len * 4;
        if (windowLen < len) windowLen = len;
        if (windowLen > kBufferSize / 2) windowLen = kBufferSize / 2;
        int maxOff = windowLen - len;
        if (maxOff < 1) maxOff = 1;
        float r = (lRandFloat() + 1.0f) * 0.5f;
        if (r < 0.0f) r = 0.0f;
        if (r > 1.0f) r = 1.0f;
        s.shuffleOffset = (int)(r * (float)maxOff);
      }

      float wetL = processEffect(inL[i], s, 0, effType, effParam, s.stepProgress);
      float wetR = processEffect(inR[i], s, 1, effType, effParam, s.stepProgress);

      // Per-channel step-boundary crossfade. The transition is shared
      // (sequencer set both counters together) but the blend math uses
      // each channel's own prevOutput so the fades are independent.
      for (int c = 0; c < 2; c++)
      {
        float &w = (c == 0) ? wetL : wetR;
        if (s.ch[c].crossfadeCounter > 0)
        {
          float blend = (float)s.ch[c].crossfadeCounter / (float)kCrossfadeSamples;
          w = s.ch[c].prevOutput * blend + w * (1.0f - blend);
          s.ch[c].crossfadeCounter--;
        }
        else
        {
          s.ch[c].prevOutput = w;
        }
      }

      float mixedL = inL[i] * (1.0f - mix) + wetL * mix;
      float mixedR = inR[i] * (1.0f - mix) + wetR * mix;

      // Linked CPR: max-of-|L|,|R| drives a single detector; the same
      // gain reduction is applied to both channels so the stereo image
      // stays coherent under heavy compression.
      if (compActive)
      {
        float absLevel = fmaxf(fabsf(mixedL), fabsf(mixedR));
        float coeff = absLevel > s.compDetector ? compRiseCoeff : compFallCoeff;
        s.compDetector = coeff * s.compDetector + (1.0f - coeff) * absLevel;
        float levelDb = 20.0f * fast_log10(s.compDetector + 1e-10f);
        float overDb = levelDb - compThresholdDb;
        if (overDb < 0.0f) overDb = 0.0f;
        float reductionDb = overDb * (1.0f - compRatioI);
        float gain = fast_fromDb(-reductionDb) * compMakeupGain;
        mixedL *= gain;
        mixedR *= gain;
      }

      outL[i] = mixedL * outputLevel;
      outR[i] = mixedR * outputLevel;

      if (++s.vizDecimCounter >= 8)
      {
        s.vizDecimCounter = 0;
        s.vizRing[s.vizPos] = 0.5f * (outL[i] + outR[i]);
        s.vizPos = (s.vizPos + 1) & 127;
      }
    }
  }

} // namespace stolmine
