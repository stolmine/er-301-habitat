#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>

namespace stolmine
{

  static const int kMaxTaps = 16;
  static const int kGrainsPerTap = 3;
  static const int kSineLUTSize = 256;

  enum TapFilterType
  {
    TAP_FILTER_OFF = 0,
    TAP_FILTER_LP,
    TAP_FILTER_BP,
    TAP_FILTER_HP,
    TAP_FILTER_NOTCH,
    TAP_FILTER_COUNT
  };

  class MultitapDelay : public od::Object
  {
  public:
    MultitapDelay();
    virtual ~MultitapDelay();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mXformGate{"XformGate"};
    od::Outlet mOut{"Out"};
    od::Outlet mOutR{"OutR"};

    od::Parameter mMasterTime{"MasterTime", 0.5f};
    od::Parameter mFeedback{"Feedback", 0.3f};
    od::Parameter mFeedbackTone{"FeedbackTone", 0.0f};
    od::Parameter mMix{"Mix", 0.5f};
    od::Parameter mTapCount{"TapCount", 4.0f};
    od::Parameter mVOctPitch{"VOctPitch", 0.0f};
    od::Parameter mSkew{"Skew", 0.0f};
    od::Parameter mGrainSize{"GrainSize", 0.5f};
    od::Parameter mInputLevel{"InputLevel", 1.0f};
    od::Parameter mOutputLevel{"OutputLevel", 1.0f};
    od::Parameter mTanhAmt{"TanhAmt", 0.0f};

    od::Parameter mXformTarget{"XformTarget", 0.0f};
    od::Parameter mXformDepth{"XformDepth", 0.5f};
    od::Parameter mXformSpread{"XformSpread", 0.5f};

    // Edit buffers for tap list
    od::Parameter mEditTapTime{"EditTapTime", 0.5f};
    od::Parameter mEditTapLevel{"EditTapLevel", 1.0f};
    od::Parameter mEditTapPan{"EditTapPan", 0.0f};
    od::Parameter mEditTapPitch{"EditTapPitch", 0.0f};

    // Edit buffers for filter list
    od::Parameter mEditFilterCutoff{"EditFilterCutoff", 10000.0f};
    od::Parameter mEditFilterQ{"EditFilterQ", 0.5f};
    od::Parameter mEditFilterType{"EditFilterType", 0.0f};
#endif

    // SWIG-visible: tap accessors
    float getTapTime(int i);
    void setTapTime(int i, float v);
    float getTapLevel(int i);
    void setTapLevel(int i, float v);
    float getTapPan(int i);
    void setTapPan(int i, float v);

    float getTapPitch(int i);
    void setTapPitch(int i, float v);

    // SWIG-visible: filter accessors
    float getFilterCutoff(int i);
    void setFilterCutoff(int i, float v);
    float getFilterQ(int i);
    void setFilterQ(int i, float v);
    int getFilterType(int i);
    void setFilterType(int i, int v);

    // Edit buffer load/store
    void loadTap(int i);
    void storeTap(int i);
    void loadFilter(int i);
    void storeFilter(int i);

    int getTapCount();
    float getTapEnergy(int i);

    // Xform
    void fireRandomize();
    void setTopLevelBias(int which, od::Parameter *param);

    // Buffer allocation
    float allocateTimeUpTo(float seconds);
    float maximumDelayTime();

  private:
    struct Internal;
    Internal *mpInternal;

    int mCachedTapCount = 4;
    int mLastLoadedTap = 0;
    int mLastLoadedFilter = 0;
    int mMaxDelayInSamples = 0;

    // Dirty check for tap distribution
    int mLastTapCount = -1;
    float mLastSkew = -999.0f;
    float mLastMasterTime = -1.0f;
    float mCachedDelaySamples[kMaxTaps];
    float mCachedPanL[kMaxTaps];
    float mCachedPanR[kMaxTaps];

    // Xform gate state
    bool mXformGateWasHigh = false;
    bool mManualFire = false;

    // Bias parameter refs for top-level randomization from gate trigger
    od::Parameter *mBiasMasterTime = 0;
    od::Parameter *mBiasFeedback = 0;
    od::Parameter *mBiasFeedbackTone = 0;
    od::Parameter *mBiasSkew = 0;
    od::Parameter *mBiasGrainSize = 0;
    od::Parameter *mBiasTapCount = 0;

    void applyRandomize();

#ifndef SWIGLUA
    bool allocate(int samples);
    void deallocate();
#endif
  };

} // namespace stolmine
