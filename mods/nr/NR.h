#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>


namespace nr
{

  // 32 prime 16-bit rhythm patterns (from teletype / Noise Engineering)
  static const uint16_t table_nr[32] = {
      0x8888, 0x888A, 0x8892, 0x8894, 0x88A2, 0x88A4,
      0x8912, 0x8914, 0x8922, 0x8924, 0x8A8A, 0x8AAA,
      0x9292, 0x92AA, 0x94AA, 0x952A, 0x8282, 0x828A,
      0x8292, 0x82A2, 0x8484, 0x848A, 0x8492, 0x8494,
      0x84A2, 0x84A4, 0x850A, 0x8512, 0x8514, 0x8522,
      0x8524, 0x8544};

  class NR : public od::Object
  {
  public:
    NR();
    virtual ~NR();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mClock{"Clock"};
    od::Inlet mReset{"Reset"};
    od::Outlet mOut{"Out"};

    od::Parameter mPrime{"Prime", 0.0f};
    od::Parameter mMask{"Mask", 0.0f};
    od::Parameter mFactor{"Factor", 1.0f};
    od::Parameter mLength{"Length", 16.0f};
    od::Parameter mWidth{"Width", 0.5f};

#endif

    int getStep() { return mStep; }
    int getLength() { return mCachedLength; }
    bool isSet(int i);

#ifndef SWIGLUA
  private:
    int mStep = 0;
    int mCachedLength = 16;

    bool mClockWasHigh = false;
    bool mResetWasHigh = false;

    int mGateSamplesRemaining = 0;
    int mClockPeriodSamples = 0;
    int mSamplesSinceLastClock = 0;

    uint16_t mCachedPattern = 0;
    int mCachedPrime = -1;
    int mCachedMask = -1;
    int mCachedFactor = -1;

    uint16_t computePattern(int prime, int mask, int factor);
#endif
  };

} // namespace nr
