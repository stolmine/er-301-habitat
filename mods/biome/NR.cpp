// NR — gate sequencer inspired by the Noise Engineering Numeric Repetitor
// Original implementation for ER-301

#include "NR.h"
#include <od/config.h>
#include <hal/ops.h>

namespace nr
{

  NR::NR()
  {
    addInput(mClock);
    addInput(mReset);
    addOutput(mOut);
    addParameter(mPrime);
    addParameter(mMask);
    addParameter(mFactor);
    addParameter(mLength);
    addParameter(mWidth);
  }

  NR::~NR() {}

  uint16_t NR::computePattern(int prime, int mask, int factor)
  {
    uint16_t rhythm = table_nr[prime];
    switch (mask)
    {
    case 1:
      rhythm &= 0x0F0F;
      break;
    case 2:
      rhythm &= 0xF003;
      break;
    case 3:
      rhythm &= 0x01F0;
      break;
    default:
      break;
    }
    uint32_t modified = (uint32_t)rhythm * (uint32_t)factor;
    return (uint16_t)((modified & 0xFFFF) | (modified >> 16));
  }

  bool NR::isSet(int i)
  {
    int prime = (int)CLAMP(0, 31, (int)mPrime.value());
    int mask = (int)CLAMP(0, 3, (int)mMask.value());
    int factor = (int)CLAMP(0, 16, (int)mFactor.value());
    if (prime != mCachedPrime || mask != mCachedMask || factor != mCachedFactor)
    {
      mCachedPattern = computePattern(prime, mask, factor);
      mCachedPrime = prime;
      mCachedMask = mask;
      mCachedFactor = factor;
    }
    int length = (int)CLAMP(1, 16, (int)mLength.value());
    int step = ((i % length) + length) % length;
    return (mCachedPattern >> (15 - step)) & 1;
  }

  void NR::process()
  {
    float *clock = mClock.buffer();
    float *reset = mReset.buffer();
    float *out = mOut.buffer();

    int prime = (int)CLAMP(0, 31, (int)mPrime.value());
    int mask = (int)CLAMP(0, 3, (int)mMask.value());
    int factor = (int)CLAMP(0, 16, (int)mFactor.value());
    int length = (int)CLAMP(1, 16, (int)mLength.value());
    float width = CLAMP(0.0f, 1.0f, mWidth.value());

    mCachedLength = length;

    if (prime != mCachedPrime || mask != mCachedMask || factor != mCachedFactor)
    {
      mCachedPattern = computePattern(prime, mask, factor);
      mCachedPrime = prime;
      mCachedMask = mask;
      mCachedFactor = factor;
    }

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
        mGateSamplesRemaining = 0;
      }

      if (clockRise)
      {
        // Measure clock period
        if (mSamplesSinceLastClock > 0)
        {
          mClockPeriodSamples = mSamplesSinceLastClock;
        }
        mSamplesSinceLastClock = 0;

        // Evaluate current step
        bool active = (mCachedPattern >> (15 - mStep)) & 1;

        if (active)
        {
          int gateSamples;
          if (mClockPeriodSamples > 0)
          {
            gateSamples = (int)(width * mClockPeriodSamples);
          }
          else
          {
            gateSamples = 48; // ~1ms default at 48kHz
          }
          if (gateSamples < 1)
            gateSamples = 1;
          mGateSamplesRemaining = gateSamples;
        }

        // Advance step
        mStep = (mStep + 1) % length;
      }

      mSamplesSinceLastClock++;

      // Output
      float value = 0.0f;
      if (mGateSamplesRemaining > 0)
      {
        value = 1.0f;
        mGateSamplesRemaining--;
      }

      out[i] = value;
    }
  }

} // namespace nr
