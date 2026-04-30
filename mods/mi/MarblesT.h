#pragma once

#include <od/objects/Object.h>

namespace mi
{

  class MarblesT : public od::Object
  {
  public:
    MarblesT();
    virtual ~MarblesT();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mClock{"Clock"};
    od::Inlet mReset{"Reset"};
    od::Outlet mOut{"Out"};

    od::Parameter mJitter{"Jitter", 0.0f};
    od::Parameter mDejaVu{"Deja Vu", 0.0f};
    od::Parameter mLength{"Length", 8.0f};
    od::Parameter mOutput{"Output", 0.5f};
    od::Option mModel{"Model", 0};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace mi
