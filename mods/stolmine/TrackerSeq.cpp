// TrackerSeq -- 64-step CV tracker sequencer for ER-301

#include "TrackerSeq.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

namespace stolmine
{

  // Simple LCG random for deviation
  static uint32_t sRandState = 12345;
  static inline float randFloat()
  {
    sRandState = sRandState * 1664525u + 1013904223u;
    return (float)(int32_t)(sRandState >> 1) / (float)0x40000000 ; // -1 to +1
  }

  struct TrackerSeq::Internal
  {
    float offset[kMaxSteps];
    int length[kMaxSteps];
    float deviation[kMaxSteps];

    void Init()
    {
      for (int i = 0; i < kMaxSteps; i++)
      {
        offset[i] = 0.0f;
        length[i] = 1;
        deviation[i] = 0.0f;
      }
    }
  };

  TrackerSeq::TrackerSeq()
  {
    addInput(mClock);
    addInput(mReset);
    addOutput(mOut);
    addParameter(mSlew);
    addParameter(mSeqLength);
    addParameter(mLoopLength);
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

  void TrackerSeq::process()
  {
    Internal &s = *mpInternal;

    float *clock = mClock.buffer();
    float *reset = mReset.buffer();
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

          // Compute deviation on step change (new random per step transition)
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
