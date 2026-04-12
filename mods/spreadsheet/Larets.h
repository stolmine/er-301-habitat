#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>

namespace stolmine
{

  class Larets : public od::Object
  {
  public:
    Larets();
    virtual ~Larets();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mClock{"Clock"};
    od::Inlet mReset{"Reset"};
    od::Inlet mTransform{"Transform"};
    od::Outlet mOut{"Out"};

    od::Parameter mStepCount{"StepCount", 8.0f};
    od::Parameter mSkew{"Skew", 0.0f};
    od::Parameter mMix{"Mix", 0.5f};
    od::Parameter mInputLevel{"InputLevel", 1.0f};
    od::Parameter mOutputLevel{"OutputLevel", 1.0f};
    od::Parameter mCompressAmt{"TanhAmt", 0.0f};
    od::Parameter mClockDiv{"ClockDiv", 1.0f};
    od::Parameter mTransformFunc{"TransformFunc", 0.0f};
    od::Parameter mTransformDepth{"TransformDepth", 0.5f};
    od::Parameter mLoopLength{"LoopLength", 0.0f};

    od::Parameter mEditType{"EditType", 0.0f};
    od::Parameter mEditParam{"EditParam", 0.0f};
    od::Parameter mEditTicks{"EditTicks", 1.0f};
#endif

    // SWIG-visible
    int getStep() { return mStep; }
    int getStepCount();

    int getStepType(int i);
    void setStepType(int i, int v);
    float getStepParam(int i);
    void setStepParam(int i, float v);
    int getStepTicks(int i);
    void setStepTicks(int i, int v);

    void loadStep(int i);
    void storeStep(int i);

    void fireTransform();
    int getActiveStep() { return mStep; }
    float getEffectiveTickCount(int i);
    float getClockPeriodSeconds();

#ifndef SWIGLUA
  private:
    struct Internal;
    Internal *mpInternal;

    int mStep = 0;
    int mTickCount = 0;
    int mDivCount = 0;
    bool mClockWasHigh = false;
    bool mResetWasHigh = false;
    bool mTransformWasHigh = false;
    bool mManualFire = false;
    int mClockPeriodSamples = 0;
    int mSamplesSinceLastClock = 0;

    void applyTransform();
    float processEffect(float input, int type, float param, float stepProgress);
#endif
  };

} // namespace stolmine
