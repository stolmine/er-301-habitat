#pragma once

#include <od/objects/Object.h>

namespace mi
{

  class RingsVoice : public od::Object
  {
  public:
    RingsVoice();
    virtual ~RingsVoice();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mInput{"In"};
    od::Inlet mVOct{"V/Oct"};
    od::Inlet mStrum{"Strum"};
    od::Outlet mOut{"Out"};
    od::Outlet mAux{"Aux"};

    od::Parameter mStructure{"Structure", 0.5f};
    od::Parameter mBrightness{"Brightness", 0.5f};
    od::Parameter mDamping{"Damping", 0.5f};
    od::Parameter mPosition{"Position", 0.5f};
    od::Parameter mModel{"Model", 0.0f};
    od::Parameter mFreq{"Freq", 0.0f};

    od::Option mPolyphony{"Polyphony", 0};
    od::Option mEasterEgg{"Easter Egg", 0};
    od::Option mInternalExciter{"Int Exciter", 1};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace mi
