#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class Transport : public od::Object
  {
  public:
    Transport();
    virtual ~Transport();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mRunGate{"Run"};
    od::Outlet mOutput{"Out"};
    od::Parameter mRate{"Rate", 2.0f}; // Hz
#endif

  private:
    float mPhase = 0.0f;
    bool mRunning = false;
  };

} // namespace stolmine
