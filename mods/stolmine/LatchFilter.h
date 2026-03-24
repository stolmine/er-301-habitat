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
    od::Outlet mOut{"Out"};
    od::Parameter mCutoff{"Cutoff", 1000.0f};
    od::Parameter mResonance{"Resonance", 0.5f};
    od::Option mMode{"Mode", 0}; // 0=LP, 1=HP
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
