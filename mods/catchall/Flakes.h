#pragma once

#include <od/objects/Object.h>
#include <od/config.h>

namespace stolmine
{

  class Flakes : public od::Object
  {
  public:
    Flakes();
    virtual ~Flakes();

    // SWIG-visible
    float allocateTimeUpTo(float seconds);

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mFreeze{"Freeze"};
    od::Outlet mOut{"Out"};

    od::Parameter mDepth{"Depth", 0.5f};
    od::Parameter mDelay{"Delay", 0.25f};
    od::Parameter mWarble{"Warble", 0.24f};
    od::Parameter mNoise{"Noise", 0.1f};
    od::Parameter mDryWet{"DryWet", 0.5f};

  private:
    struct Internal;
    Internal *mpInternal;
#endif
  };

} // namespace stolmine
