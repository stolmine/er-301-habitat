#pragma once

#include <od/objects/Object.h>

namespace mi
{

  class MarblesX : public od::Object
  {
  public:
    MarblesX();
    virtual ~MarblesX();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mClock{"Clock"};
    od::Inlet mReset{"Reset"};
    od::Outlet mOut{"Out"};

    od::Parameter mSpread{"Spread", 0.5f};
    od::Parameter mBias{"Bias", 0.5f};
    od::Parameter mSteps{"Steps", 0.5f};
    od::Parameter mDejaVu{"Deja Vu", 0.0f};
    od::Parameter mLength{"Length", 8.0f};
    od::Parameter mOutput{"Output", 0.0f};
    od::Option mControlMode{"Control Mode", 0};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace mi
