#pragma once

#include <od/objects/Object.h>
#include <od/config.h>

namespace stolmine
{

  class BBCut : public od::Object
  {
  public:
    BBCut();
    virtual ~BBCut();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mClock{"Clock"};
    od::Outlet mOut{"Out"};

    od::Parameter mDensity{"Density", 0.5f};
    od::Parameter mBlockSize{"BlockSize", 0.5f};
    od::Parameter mBlockMax{"BlockMax", 4.0f};
    od::Parameter mRepeatCount{"RepeatCount", 8.0f};
    od::Parameter mRitardBias{"RitardBias", 0.5f};
    od::Parameter mBlend{"Blend", 0.5f};
    od::Parameter mAccel{"Accel", 0.9f};

    od::Parameter mSubdiv{"Subdiv", 8.0f};
    od::Parameter mPhraseMin{"PhraseMin", 2.0f};
    od::Parameter mPhraseMax{"PhraseMax", 4.0f};

    od::Parameter mDutyCycle{"DutyCycle", 1.0f};
    od::Parameter mAmpMin{"AmpMin", 0.8f};
    od::Parameter mAmpMax{"AmpMax", 1.0f};
    od::Parameter mFade{"Fade", 0.005f};

    od::Parameter mMix{"Mix", 1.0f};
    od::Parameter mInputLevel{"InputLevel", 1.0f};
    od::Parameter mOutputLevel{"OutputLevel", 1.0f};
    od::Parameter mTanhAmt{"TanhAmt", 0.0f};
#endif

    int getPhraseBars();
    int getPhrasePosition();
    int getPhraseLength();
    float getOutputSample(int idx);

  private:
    struct Internal;
    Internal *mpInternal;

    bool mClockWasHigh = false;
    int mClockPeriodSamples = 24000;
    int mSamplesSinceLastClock = 0;

    void advanceUnit();
    void choosePhraseLength();
    void chooseBlock(int unitsLeft);
  };

}
