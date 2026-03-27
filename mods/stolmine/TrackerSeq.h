#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>

namespace stolmine
{

  static const int kMaxSteps = 64;

  enum TransformFunc
  {
    XFORM_ADD = 0,
    XFORM_SUB,
    XFORM_MUL,
    XFORM_DIV,
    XFORM_MOD,
    XFORM_REVERSE,
    XFORM_ROTATE,
    XFORM_INVERT,
    XFORM_RANDOM,
    XFORM_COUNT
  };

  enum TransformScope
  {
    SCOPE_OFFSET = 0,
    SCOPE_LENGTH,
    SCOPE_DEVIATION,
    SCOPE_ALL,
    SCOPE_COUNT
  };

  class TrackerSeq : public od::Object
  {
  public:
    TrackerSeq();
    virtual ~TrackerSeq();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mClock{"Clock"};
    od::Inlet mReset{"Reset"};
    od::Inlet mTransform{"Transform"};
    od::Outlet mOut{"Out"};

    od::Parameter mSlew{"Slew", 0.0f};
    od::Parameter mSeqLength{"SeqLength", 16.0f};
    od::Parameter mLoopLength{"LoopLength", 0.0f};

    od::Parameter mTransformFunc{"TransformFunc", 0.0f};
    od::Parameter mTransformFactor{"TransformFactor", 1.0f};
    od::Parameter mTransformScope{"TransformScope", 0.0f};

    // Edit buffer: scratch params for Readout binding
    od::Parameter mEditOffset{"EditOffset", 0.0f};
    od::Parameter mEditLength{"EditLength", 1.0f};
    od::Parameter mEditDeviation{"EditDeviation", 0.0f};
#endif

    // SWIG-visible
    int getStep() { return mStep; }
    int getSeqLength() { return mCachedSeqLength; }
    int getLoopLength() { return mCachedLoopLength; }
    int getTotalTicks();

    float getStepOffset(int i);
    void setStepOffset(int i, float v);
    int getStepLength(int i);
    void setStepLength(int i, int v);
    float getStepDeviation(int i);
    void setStepDeviation(int i, float v);

    void loadStep(int i);
    void storeStep(int i);

    void fireTransform();
    int getLastTransformFunc() { return mLastTransformFunc; }
    int getLastTransformFactor() { return mLastTransformFactor; }
    int getLastTransformScope() { return mLastTransformScope; }

    void snapshotSave();
    void snapshotRestore();

#ifndef SWIGLUA
  private:
    struct Internal;
    Internal *mpInternal;

    int mStep = 0;
    int mTickCount = 0;
    int mCachedSeqLength = 16;
    int mCachedLoopLength = 0;
    float mCurrentOutput = 0.0f;
    float mDeviationOffset = 0.0f;

    bool mClockWasHigh = false;
    bool mResetWasHigh = false;
    bool mTransformWasHigh = false;
    bool mManualFire = false;

    int mLastTransformFunc = -1;
    int mLastTransformFactor = 0;
    int mLastTransformScope = 0;

    void applyTransform();
#endif
  };

} // namespace stolmine
