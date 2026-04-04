#pragma once

#include <od/objects/Object.h>
#include <od/extras/Random.h>

namespace stolmine
{

  class PingableScaledRandom : public od::Object
  {
  public:
    PingableScaledRandom();
    virtual ~PingableScaledRandom();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mTrigger{"Trigger"};
    od::Outlet mOutput{"Out"};
    od::Parameter mScale{"Scale", 1.0f};
    od::Parameter mOffset{"Offset", 0.0f};
    od::Parameter mLevels{"Levels", 0.0f}; // 0 = no quantize, >0 = quantize to N levels
#endif

  private:
    float mHeldValue = 0.0f;
    bool mTrigWasHigh = false;
  };

} // namespace stolmine
