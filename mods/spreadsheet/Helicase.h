#pragma once

#include <od/objects/Object.h>
#include <od/graphics/Graphic.h>
#include <od/config.h>
#include <math.h>

namespace stolmine
{

  class Helicase : public od::Object
  {
  public:
    Helicase();
    virtual ~Helicase();

    // SWIG-visible
    float getOutputSample(int idx);
    float getModulatorSample(int idx);
    float getCarrierPhase();
    float getCarrierOutput();
    float getDiscIndex();
    float getDiscType();
    bool isLinFM();
    void toggleLinFM();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mVOct{"V/Oct"};
    od::Inlet mSync{"Sync"};
    od::Outlet mOut{"Out"};

    od::Parameter mFundamental{"Fundamental", 110.0f};
    od::Parameter mModMix{"ModMix", 0.5f};
    od::Parameter mModIndex{"ModIndex", 1.0f};
    od::Parameter mDiscIndex{"DiscIndex", 0.0f};
    od::Parameter mDiscType{"DiscType", 0.0f};
    od::Parameter mRatio{"Ratio", 2.0f};
    od::Parameter mFeedback{"Feedback", 0.0f};
    od::Parameter mModShape{"ModShape", 0.0f};
    od::Parameter mFine{"Fine", 0.0f};
    od::Parameter mLevel{"Level", 0.5f};
    od::Parameter mCarrierShape{"CarrierShape", 0.0f};
    od::Parameter mSyncThreshold{"SyncThreshold", 0.0f};
    od::Option mLinExpo{"LinExpo", 2}; // 1=lin, 2=expo
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
