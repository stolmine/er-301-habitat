// TrackerSeq -- 64-step CV tracker sequencer for ER-301

#include "TrackerSeq.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

namespace stolmine
{

  struct TrackerSeq::Internal
  {
    float offset[kMaxSteps];
    int length[kMaxSteps];
    float slew[kMaxSteps];

    void Init()
    {
      for (int i = 0; i < kMaxSteps; i++)
      {
        offset[i] = 0.0f;
        length[i] = 1;
        slew[i] = 0.0f;
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
    addParameter(mEditSlew);

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

  float TrackerSeq::getStepSlew(int i)
  {
    return mpInternal->slew[CLAMP(0, kMaxSteps - 1, i)];
  }

  void TrackerSeq::setStepSlew(int i, float v)
  {
    mpInternal->slew[CLAMP(0, kMaxSteps - 1, i)] = CLAMP(0.0f, 1.0f, v);
  }

  void TrackerSeq::loadStep(int i)
  {
    i = CLAMP(0, kMaxSteps - 1, i);
    mEditOffset.hardSet(mpInternal->offset[i]);
    mEditLength.hardSet((float)mpInternal->length[i]);
    mEditSlew.hardSet(mpInternal->slew[i]);
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
    mpInternal->slew[i] = CLAMP(0.0f, 1.0f, mEditSlew.value());
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
            // Loop within window of loopLen steps
            int loopStart = mStep - (mStep % loopLen);
            mStep = loopStart + ((mStep - loopStart + 1) % loopLen);
          }
          else
          {
            mStep = (mStep + 1) % seqLen;
          }
        }
      }

      // Target output from current step
      float target = s.offset[mStep % seqLen];

      // Compute slew coefficient
      float perStepSlew = s.slew[mStep % seqLen];
      float effectiveSlew = perStepSlew > 0.0f ? perStepSlew * globalSlew : globalSlew;

      if (effectiveSlew > 0.001f)
      {
        // One-pole smoothing: higher slew = slower response
        float alpha = effectiveSlew * effectiveSlew;
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
