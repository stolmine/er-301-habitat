#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class ConstantRandom : public od::Object
  {
  public:
    ConstantRandom();
    virtual ~ConstantRandom();

#ifndef SWIGLUA
    virtual void process();
    od::Outlet mOutput{"Out"};
    od::Parameter mRate{"Rate", 5.0f};
    od::Parameter mSlew{"Slew", 0.0f};
    od::Parameter mLevel{"Level", 1.0f};
#endif

  private:
    float mValue = 0.0f;
    float mTarget = 0.0f;
    float mPhase = 0.0f;
    uint32_t mSeed = 98765;
  };

} // namespace stolmine
