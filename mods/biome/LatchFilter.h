#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class LatchFilter : public od::Object
  {
  public:
    LatchFilter();
    virtual ~LatchFilter();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};
    od::Parameter mFundamental{"Fundamental", 0.0f};
    od::Parameter mResonance{"Resonance", 0.5f};
    od::Parameter mMode{"Mode", 0.0f}; // 0=LP, 1=HP
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
