// TrackerSeq -- 64-step CV tracker sequencer for ER-301

#include "TrackerSeq.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

namespace stolmine
{

  // Simple LCG random for deviation and random transform
  static uint32_t sRandState = 12345;
  static inline float randFloat()
  {
    sRandState = sRandState * 1664525u + 1013904223u;
    return (float)(int32_t)(sRandState >> 1) / (float)0x40000000; // -1 to +1
  }

  struct TrackerSeq::Internal
  {
    float offset[kMaxSteps];
    int length[kMaxSteps];
    float deviation[kMaxSteps];

    // Snapshot for non-destructive transforms
    float snapOffset[kMaxSteps];
    int snapLength[kMaxSteps];
    float snapDeviation[kMaxSteps];
    bool hasSnapshot;

    void Init()
    {
      for (int i = 0; i < kMaxSteps; i++)
      {
        offset[i] = 0.0f;
        length[i] = 1;
        deviation[i] = 0.0f;
      }
      hasSnapshot = false;
    }
  };

  TrackerSeq::TrackerSeq()
  {
    addInput(mClock);
    addInput(mReset);
    addInput(mTransform);
    addOutput(mOut);
    addParameter(mSlew);
    addParameter(mSeqLength);
    addParameter(mLoopLength);
    addParameter(mTransformFunc);
    addParameter(mTransformFactor);
    addParameter(mTransformScope);
    addParameter(mEditOffset);
    addParameter(mEditLength);
    addParameter(mEditDeviation);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  TrackerSeq::~TrackerSeq()
  {
    delete mpInternal;
  }

  float TrackerSeq::getStepOffset(int i)
  {
    return mpInternal->offset[CLAMP(0, kMaxSteps - 1, i)];
  }

  void TrackerSeq::setStepOffset(int i, float v)
  {
    mpInternal->offset[CLAMP(0, kMaxSteps - 1, i)] = v;
  }

  int TrackerSeq::getStepLength(int i)
  {
    return mpInternal->length[CLAMP(0, kMaxSteps - 1, i)];
  }

  void TrackerSeq::setStepLength(int i, int v)
  {
    mpInternal->length[CLAMP(0, kMaxSteps - 1, i)] = MAX(1, v);
  }

  float TrackerSeq::getStepDeviation(int i)
  {
    return mpInternal->deviation[CLAMP(0, kMaxSteps - 1, i)];
  }

  void TrackerSeq::setStepDeviation(int i, float v)
  {
    mpInternal->deviation[CLAMP(0, kMaxSteps - 1, i)] = CLAMP(0.0f, 1.0f, v);
  }

  void TrackerSeq::loadStep(int i)
  {
    i = CLAMP(0, kMaxSteps - 1, i);
    mEditOffset.hardSet(mpInternal->offset[i]);
    mEditLength.hardSet((float)mpInternal->length[i]);
    mEditDeviation.hardSet(mpInternal->deviation[i]);
  }

  int TrackerSeq::getTotalTicks()
  {
    int total = 0;
    for (int i = 0; i < mCachedSeqLength; i++)
      total += mpInternal->length[i];
    return total;
  }

  void TrackerSeq::storeStep(int i)
  {
    i = CLAMP(0, kMaxSteps - 1, i);
    mpInternal->offset[i] = mEditOffset.value();
    mpInternal->length[i] = MAX(1, (int)(mEditLength.value() + 0.5f));
    mpInternal->deviation[i] = CLAMP(0.0f, 1.0f, mEditDeviation.value());
  }

  void TrackerSeq::fireTransform()
  {
    mManualFire = true;
  }

  void TrackerSeq::snapshotSave()
  {
    Internal &s = *mpInternal;
    memcpy(s.snapOffset, s.offset, sizeof(s.offset));
    memcpy(s.snapLength, s.length, sizeof(s.length));
    memcpy(s.snapDeviation, s.deviation, sizeof(s.deviation));
    s.hasSnapshot = true;
  }

  void TrackerSeq::snapshotRestore()
  {
    Internal &s = *mpInternal;
    if (!s.hasSnapshot)
      return;
    memcpy(s.offset, s.snapOffset, sizeof(s.offset));
    memcpy(s.length, s.snapLength, sizeof(s.length));
    memcpy(s.deviation, s.snapDeviation, sizeof(s.deviation));
    s.hasSnapshot = false;
  }

  // Helper: apply a scalar transform to a float array
  static void transformFloatArray(float *arr, int len, int func, int factor)
  {
    switch (func)
    {
    case XFORM_ADD:
      for (int i = 0; i < len; i++)
        arr[i] += (float)factor;
      break;
    case XFORM_SUB:
      for (int i = 0; i < len; i++)
        arr[i] -= (float)factor;
      break;
    case XFORM_MUL:
      for (int i = 0; i < len; i++)
        arr[i] *= (float)factor;
      break;
    case XFORM_DIV:
      for (int i = 0; i < len; i++)
        arr[i] /= (float)factor;
      break;
    case XFORM_MOD:
      for (int i = 0; i < len; i++)
        arr[i] = fmodf(arr[i], (float)factor);
      break;
    case XFORM_REVERSE:
      for (int lo = 0, hi = len - 1; lo < hi; lo++, hi--)
      {
        float t = arr[lo];
        arr[lo] = arr[hi];
        arr[hi] = t;
      }
      break;
    case XFORM_ROTATE:
    {
      int rot = ((factor % len) + len) % len;
      if (rot > 0)
      {
        // Reverse-reverse-reverse rotation
        auto rev = [&](int a, int b)
        {
          while (a < b)
          {
            float t = arr[a];
            arr[a] = arr[b];
            arr[b] = t;
            a++;
            b--;
          }
        };
        rev(0, len - 1);
        rev(0, rot - 1);
        rev(rot, len - 1);
      }
      break;
    }
    case XFORM_INVERT:
      for (int i = 0; i < len; i++)
        arr[i] = -arr[i];
      break;
    case XFORM_RANDOM:
      for (int i = 0; i < len; i++)
        arr[i] = randFloat() * (float)factor;
      break;
    }
  }

  // Helper: apply transform to int array (for length)
  static void transformIntArray(int *arr, int len, int func, int factor)
  {
    switch (func)
    {
    case XFORM_ADD:
      for (int i = 0; i < len; i++)
        arr[i] = MAX(1, arr[i] + factor);
      break;
    case XFORM_SUB:
      for (int i = 0; i < len; i++)
        arr[i] = MAX(1, arr[i] - factor);
      break;
    case XFORM_MUL:
      for (int i = 0; i < len; i++)
        arr[i] = MAX(1, arr[i] * factor);
      break;
    case XFORM_DIV:
      for (int i = 0; i < len; i++)
        arr[i] = MAX(1, arr[i] / factor);
      break;
    case XFORM_MOD:
      for (int i = 0; i < len; i++)
        arr[i] = MAX(1, arr[i] % factor);
      break;
    case XFORM_REVERSE:
      for (int lo = 0, hi = len - 1; lo < hi; lo++, hi--)
      {
        int t = arr[lo];
        arr[lo] = arr[hi];
        arr[hi] = t;
      }
      break;
    case XFORM_ROTATE:
    {
      int rot = ((factor % len) + len) % len;
      if (rot > 0)
      {
        auto rev = [&](int a, int b)
        {
          while (a < b)
          {
            int t = arr[a];
            arr[a] = arr[b];
            arr[b] = t;
            a++;
            b--;
          }
        };
        rev(0, len - 1);
        rev(0, rot - 1);
        rev(rot, len - 1);
      }
      break;
    }
    case XFORM_INVERT:
      for (int i = 0; i < len; i++)
        arr[i] = MAX(1, -arr[i]);
      break;
    case XFORM_RANDOM:
      for (int i = 0; i < len; i++)
      {
        sRandState = sRandState * 1664525u + 1013904223u;
        arr[i] = MAX(1, (int)(sRandState % (uint32_t)factor) + 1);
      }
      break;
    }
  }

  void TrackerSeq::applyTransform()
  {
    Internal &s = *mpInternal;
    int func = CLAMP(0, (int)XFORM_COUNT - 1, (int)(mTransformFunc.value() + 0.5f));
    int factor = MAX(1, (int)(mTransformFactor.value() + 0.5f));
    int scope = CLAMP(0, (int)SCOPE_COUNT - 1, (int)(mTransformScope.value() + 0.5f));
    int seqLen = mCachedSeqLength;

    if (scope == SCOPE_OFFSET || scope == SCOPE_ALL)
      transformFloatArray(s.offset, seqLen, func, factor);

    if (scope == SCOPE_LENGTH || scope == SCOPE_ALL)
      transformIntArray(s.length, seqLen, func, factor);

    if (scope == SCOPE_DEVIATION || scope == SCOPE_ALL)
      transformFloatArray(s.deviation, seqLen, func, factor);

    mLastTransformFunc = func;
    mLastTransformFactor = factor;
    mLastTransformScope = scope;
  }

  void TrackerSeq::process()
  {
    Internal &s = *mpInternal;

    float *clock = mClock.buffer();
    float *reset = mReset.buffer();
    float *xform = mTransform.buffer();
    float *out = mOut.buffer();

    int seqLen = CLAMP(1, kMaxSteps, (int)(mSeqLength.value() + 0.5f));
    int loopLen = CLAMP(0, seqLen, (int)(mLoopLength.value() + 0.5f));
    float globalSlew = CLAMP(0.0f, 1.0f, mSlew.value());

    mCachedSeqLength = seqLen;
    mCachedLoopLength = loopLen;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool clockHigh = clock[i] > 0.0f;
      bool resetHigh = reset[i] > 0.0f;
      bool clockRise = clockHigh && !mClockWasHigh;
      bool resetRise = resetHigh && !mResetWasHigh;

      mClockWasHigh = clockHigh;
      mResetWasHigh = resetHigh;

      // Transform gate
      bool xformHigh = xform[i] > 0.0f;
      bool xformRise = xformHigh && !mTransformWasHigh;
      mTransformWasHigh = xformHigh;

      if (xformRise || mManualFire)
      {
        applyTransform();
        mManualFire = false;
      }

      if (resetRise)
      {
        mStep = 0;
        mTickCount = 0;
      }

      if (clockRise)
      {
        mTickCount++;
        int stepLen = s.length[mStep % seqLen];
        if (mTickCount >= stepLen)
        {
          mTickCount = 0;
          if (loopLen > 0)
          {
            int loopStart = mStep - (mStep % loopLen);
            mStep = loopStart + ((mStep - loopStart + 1) % loopLen);
          }
          else
          {
            mStep = (mStep + 1) % seqLen;
          }

          float dev = s.deviation[mStep % seqLen];
          mDeviationOffset = dev > 0.001f ? randFloat() * dev : 0.0f;
        }
      }

      // Target output: base offset + deviation, scaled for V/Oct
      float base = s.offset[mStep % seqLen];
      float target = (base + mDeviationOffset) * 0.1f;

      // Global slew: one-pole smoothing
      if (globalSlew > 0.001f)
      {
        float alpha = globalSlew * globalSlew;
        mCurrentOutput += (target - mCurrentOutput) * (1.0f - alpha);
      }
      else
      {
        mCurrentOutput = target;
      }

      out[i] = mCurrentOutput;
    }
  }

} // namespace stolmine
