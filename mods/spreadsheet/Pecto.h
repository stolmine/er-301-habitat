#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <od/extras/LinearRamp.h>

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
    // (combSize, V/Oct) causes baseDelay to jump block-to-block,
    // producing audible zipper noise on the 24-tap multitap. Smooth
    // by linearly ramping baseDelay over a 25ms window: every time
    // the previous ramp completes, snapshot prev <- cur, set cur to
    // the latest target, reset the ramp. Per-sample inside process()
    // currentBase = fade[i] * mPrevBaseDelay + (1 - fade[i]) * mCurBaseDelay
    // and the read pointer slides smoothly to the new position.
    //
    // Doppler-style rather than crossfade (od::Delay's pattern at 24
    // taps doubled the per-sample NEON pipeline and produced trap-
    // shaped codegen on Cortex-A8 in .182/.183). For a multitap comb
    // this is also more musical -- pitch glide during sweeps matches
    // tape-delay character.
    od::LinearRamp mFade;
    float mPrevBaseDelay = 0.0f;
    float mCurBaseDelay = 0.0f;

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
