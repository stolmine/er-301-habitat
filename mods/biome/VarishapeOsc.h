#pragma once

#include <od/objects/Object.h>
#include <od/config.h>

namespace stages { class VariableShapeOscillator; }

namespace stolmine
{

  class VarishapeOsc : public od::Object
  {
  public:
    VarishapeOsc();
    virtual ~VarishapeOsc();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mVOct{"V/Oct"};
    od::Inlet mSync{"Sync"};
    od::Outlet mOutput{"Out"};
    od::Parameter mShape{"Shape", 0.0f};
    od::Parameter mFundamental{"Fundamental", 110.0f};

  private:
    stages::VariableShapeOscillator *mpOsc = nullptr;
    float *mpWorkBuffer = nullptr;
#endif
  };

} // namespace stolmine
