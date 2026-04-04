#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class GatedSlewLimiter : public od::Object
  {
  public:
    GatedSlewLimiter();
    virtual ~GatedSlewLimiter();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mInput{"In"};
    od::Inlet mGate{"Gate"};
    od::Outlet mOutput{"Out"};
    od::Parameter mTime{"Time", 0.0f};
    od::Option mDirection{"Direction", CHOICE_BOTH};
#endif

  private:
    float mPreviousValue = 0.0f;
  };

} // namespace stolmine
