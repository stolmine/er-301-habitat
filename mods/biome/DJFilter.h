#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class DJFilter : public od::Object
  {
  public:
    DJFilter();
    virtual ~DJFilter();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mInput{"In"};
    od::Outlet mOutput{"Out"};
    od::Parameter mCut{"Cut", 0.0f}; // -1 LP, 0 bypass, +1 HP
    od::Parameter mQ{"Q", 0.5f};     // 0 clean, 1 resonant
#endif

  private:
    float mIc1eq = 0.0f;
    float mIc2eq = 0.0f;
  };

} // namespace stolmine
