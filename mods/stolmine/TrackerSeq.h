#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>

namespace stolmine
{

  static const int kMaxSteps = 64;

  class TrackerSeq : public od::Object
  {
  public:
    TrackerSeq();
    virtual ~TrackerSeq();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mClock{"Clock"};
    od::Inlet mReset{"Reset"};
    od::Outlet mOut{"Out"};

    od::Parameter mSlew{"Slew", 0.0f};
    od::Parameter mSeqLength{"SeqLength", 16.0f};
    od::Parameter mLoopLength{"LoopLength", 0.0f};

    // Edit buffer: scratch params for Readout binding
    od::Parameter mEditOffset{"EditOffset", 0.0f};
    od::Parameter mEditLength{"EditLength", 1.0f};
    od::Parameter mEditSlew{"EditSlew", 0.0f};
#endif

    // SWIG-visible getters/setters for UI and serialization
    int getStep() { return mStep; }
    int getSeqLength() { return mCachedSeqLength; }
    int getLoopLength() { return mCachedLoopLength; }
    int getTotalTicks();

    float getStepOffset(int i);
    void setStepOffset(int i, float v);
    int getStepLength(int i);
    void setStepLength(int i, int v);
    float getStepSlew(int i);
    void setStepSlew(int i, float v);

    void loadStep(int i);
    void storeStep(int i);

#ifndef SWIGLUA
  private:
    struct Internal;
    Internal *mpInternal;

    int mStep = 0;
    int mTickCount = 0;
    int mCachedSeqLength = 16;
    int mCachedLoopLength = 0;
    float mCurrentOutput = 0.0f;

    bool mClockWasHigh = false;
    bool mResetWasHigh = false;
#endif
  };

} // namespace stolmine
