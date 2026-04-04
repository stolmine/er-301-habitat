#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class Canals : public od::Object
  {
  public:
    Canals();
    virtual ~Canals();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};
    od::Parameter mFundamental{"Fundamental", 0.0f};
    od::Parameter mSpan{"Span", 0.25f};
    od::Parameter mQuality{"Quality", 0.0f};
    od::Parameter mOutput{"Output", 0.0f};
    od::Parameter mMode{"Mode", 0.0f}; // 0=crossover, 1=formant
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
