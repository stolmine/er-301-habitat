#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class Integrator : public od::Object
  {
  public:
    Integrator();
    virtual ~Integrator();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mInput{"In"};
    od::Inlet mReset{"Reset"};
    od::Outlet mOutput{"Out"};
    od::Parameter mRate{"Rate", 1.0f};
    od::Parameter mLeak{"Leak", 0.0f}; // 0 = pure integrator, 1 = fast decay
#endif

  private:
    float mValue = 0.0f;
    bool mResetWasHigh = false;
  };

} // namespace stolmine
