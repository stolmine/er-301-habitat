#pragma once

#include <od/objects/Object.h>
#include <od/config.h>

namespace stolmine
{

  static const int kMaxCombTaps = 64;

  class Pecto : public od::Object
  {
  public:
    Pecto();
    virtual ~Pecto();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mVOct{"V/Oct"};
    od::Inlet mXformGate{"XformGate"};
    od::Outlet mOut{"Out"};

    od::Parameter mCombSize{"CombSize", 0.01f};
    od::Parameter mFeedback{"Feedback", 0.5f};
    od::Parameter mVOctPitch{"VOctPitch", 0.0f};
    od::Parameter mDensity{"Density", 8.0f};
    od::Parameter mPattern{"Pattern", 0.0f};
    od::Parameter mSlope{"Slope", 0.0f};
    od::Parameter mResonatorType{"ResonatorType", 0.0f};
    od::Parameter mMix{"Mix", 0.5f};
    od::Parameter mInputLevel{"InputLevel", 1.0f};
    od::Parameter mOutputLevel{"OutputLevel", 1.0f};
    od::Parameter mTanhAmt{"TanhAmt", 0.0f};
    od::Parameter mXformTarget{"XformTarget", 0.0f};
    od::Parameter mXformDepth{"XformDepth", 0.5f};
#endif

    // SWIG-visible
    float allocateTimeUpTo(float seconds);
    float maximumDelayTime();
    void fireRandomize();
    void setTopLevelBias(int which, od::Parameter *param);

  private:
    struct Internal;
    Internal *mpInternal;

    int mMaxDelayInSamples = 0;

    // Dirty-check cache (categorical: density / pattern / slope)
    int mLastDensity = -1;
    int mLastPattern = -1;
    int mLastSlope = -1;
    float mLastCombSize = -1.0f;
    float mCachedTapWeight[kMaxCombTaps];

    // Doppler-style smoother for baseDelay. Continuous knob motion
    // (combSize, V/Oct) jumps baseDelay block-to-block, producing
    // audible zipper noise on the 24-tap multitap. Smoother is a
    // simple per-sample one-pole LP (Path C in planning/pecto-zipper.md
    // -- chosen over the LinearRamp+fade-buffer Path B which crashed
    // under V/Oct + size double-modulation in .184/.185 with no
    // identifiable trap surface; the asymptotic settling is musically
    // adequate for a comb filter and removes both LinearRamp state
    // and the AudioThread::getFrame allocation per block).
    //
    // mSmoothedBaseDelay tracks baseDelay via per-sample
    // smoothedBaseDelay += (target - smoothedBaseDelay) * alpha,
    // with alpha tuned for ~25ms settling at 48kHz. Pass A uses the
    // smoothed value via vdupq_n_f32(currentBase).
    float mSmoothedBaseDelay = 0.0f;

    // Xform gate state
    bool mXformGateWasHigh = false;
    bool mManualFire = false;

    // Bias parameter refs for randomization
    od::Parameter *mBiasCombSize = 0;
    od::Parameter *mBiasFeedback = 0;
    od::Parameter *mBiasDensity = 0;
    od::Parameter *mBiasPattern = 0;
    od::Parameter *mBiasSlope = 0;
    od::Parameter *mBiasResonatorType = 0;
    od::Parameter *mBiasMix = 0;

    void recomputeTaps(int density, int pattern, int slope);
    void applyRandomize();

#ifndef SWIGLUA
    bool allocate(int samples);
    void deallocate();
#endif
  };

} // namespace stolmine
