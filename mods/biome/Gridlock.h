#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class Gridlock : public od::Object
  {
  public:
    Gridlock();
    virtual ~Gridlock();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mGate1{"Gate1"};
    od::Inlet mGate2{"Gate2"};
    od::Inlet mGate3{"Gate3"};
    od::Outlet mOutput{"Out"};
    od::Parameter mValue1{"Value1", 1.0f};
    od::Parameter mValue2{"Value2", 0.0f};
    od::Parameter mValue3{"Value3", -1.0f};
#endif

  private:
    float mHeldValue = 0.0f;
  };

} // namespace stolmine
