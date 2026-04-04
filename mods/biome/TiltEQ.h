#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class TiltEQ : public od::Object
  {
  public:
    TiltEQ();
    virtual ~TiltEQ();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mInput{"In"};
    od::Outlet mOutput{"Out"};
    od::Parameter mTilt{"Tilt", 0.0f}; // -1 dark, 0 flat, +1 bright
#endif

  private:
    float mLpState = 0.0f;
  };

} // namespace stolmine
