#pragma once

#include <od/objects/Object.h>

namespace mi
{

  class WarpsModulator : public od::Object
  {
  public:
    WarpsModulator();
    virtual ~WarpsModulator();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mCarrier{"Carrier"};
    od::Inlet mModulator{"Modulator"};
    od::Outlet mOut{"Out"};
    od::Outlet mAux{"Aux"};

    od::Parameter mAlgorithm{"Algorithm", 0.0f};
    od::Parameter mTimbre{"Timbre", 0.5f};
    od::Parameter mDrive{"Drive", 0.5f};

    od::Option mEasterEgg{"Easter Egg", 0};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace mi
