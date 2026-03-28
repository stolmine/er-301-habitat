#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>

namespace stolmine
{

  static const int kGateMaxSteps = 64;

  // NR pattern table (shared with NR unit)
  static const uint16_t gs_table_nr[32] = {
      0x8888, 0x888A, 0x8892, 0x8894, 0x88A2, 0x88A4,
      0x8912, 0x8914, 0x8922, 0x8924, 0x8A8A, 0x8AAA,
      0x9292, 0x92AA, 0x94AA, 0x952A, 0x8282, 0x828A,
      0x8292, 0x82A2, 0x8484, 0x848A, 0x8492, 0x8494,
      0x84A2, 0x84A4, 0x850A, 0x8512, 0x8514, 0x8522,
      0x8524, 0x8544};

  enum GateTransformFunc
  {
    GS_XFORM_EUCLIDEAN = 0,
    GS_XFORM_NR,
    GS_XFORM_GRIDS,
    GS_XFORM_NECKLACE,
    GS_XFORM_INVERT,
    GS_XFORM_ROTATE,
    GS_XFORM_DENSITY,
    GS_XFORM_COUNT
  };

  enum GateTransformScope
  {
    GS_SCOPE_GATE = 0,
    GS_SCOPE_LENGTH,
    GS_SCOPE_VELOCITY,
    GS_SCOPE_ALL,
    GS_SCOPE_COUNT
  };

  class GateSeq : public od::Object
  {
  public:
    GateSeq();
    virtual ~GateSeq();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mClock{"Clock"};
    od::Inlet mReset{"Reset"};
    od::Inlet mRatchet{"Ratchet"};
    od::Inlet mTransform{"Transform"};
    od::Outlet mOut{"Out"};

    od::Parameter mSeqLength{"SeqLength", 16.0f};
    od::Parameter mLoopLength{"LoopLength", 0.0f};

    // Ratchet
    od::Parameter mRatchetMult{"RatchetMult", 1.0f};
    od::Option mRatchetLenToggle{"RatchetLen", 0};
    od::Option mRatchetVelToggle{"RatchetVel", 0};

    // Transform
    od::Parameter mTransformFunc{"TransformFunc", 0.0f};
    od::Parameter mTransformParamA{"TransformParamA", 4.0f};
    od::Parameter mTransformParamB{"TransformParamB", 0.0f};
    od::Parameter mTransformScope{"TransformScope", 0.0f};

    // Edit buffer
    od::Parameter mEditGate{"EditGate", 0.0f};
    od::Parameter mEditLength{"EditLength", 1.0f};
    od::Parameter mEditVelocity{"EditVelocity", 1.0f};
#endif

    // SWIG-visible
    int getStep() { return mStep; }
    int getSeqLength() { return mCachedSeqLength; }
    int getLoopLength() { return mCachedLoopLength; }
    int getTotalTicks();

    bool getStepGate(int i);
    void setStepGate(int i, bool v);
    int getStepLength(int i);
    void setStepLength(int i, int v);
    float getStepVelocity(int i);
    void setStepVelocity(int i, float v);

    void loadStep(int i);
    void storeStep(int i);

    void fireTransform();
    int getLastTransformFunc() { return mLastTransformFunc; }
    int getLastTransformParamA() { return mLastTransformParamA; }
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

    // Gate output state
    int mGateSamplesRemaining = 0;
    float mGateAmplitude = 0.0f;

    // Clock measurement for ratchet
    int mClockPeriodSamples = 0;
    int mSamplesSinceLastClock = 0;

    // Ratchet state
    int mRatchetSubGateIndex = 0;
    int mRatchetSubGateTotal = 0;
    int mRatchetSubGateSamples = 0;
    int mRatchetSubGateRemaining = 0;
    float mRatchetBaseVelocity = 0.0f;
    int mRatchetBaseLength = 0;
    bool mRatchetActive = false;

    // Edge detection
    bool mClockWasHigh = false;
    bool mResetWasHigh = false;
    bool mTransformWasHigh = false;
    bool mManualFire = false;
    int mEditingStep = 0;

    int mLastTransformFunc = -1;
    int mLastTransformParamA = 0;
    int mLastTransformScope = 0;

    void applyTransform();
    void applyEuclidean(bool *pattern, int steps, int hits, int rotation);
    void applyNR(bool *pattern, int steps, int prime, int mask);
    void applyNecklace(bool *pattern, int steps, int density, int index);
#endif
  };

} // namespace stolmine
