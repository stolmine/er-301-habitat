#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class FadeMixer : public od::Object
  {
  public:
    FadeMixer();
    virtual ~FadeMixer();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mIn1{"In1"};
    od::Inlet mIn2{"In2"};
    od::Inlet mIn3{"In3"};
    od::Inlet mIn4{"In4"};
    od::Outlet mOutput{"Out"};
    od::Parameter mFade{"Fade", 0.0f};   // 0-1, position across 4 inputs
    od::Parameter mLevel{"Level", 1.0f}; // output level
#endif
  };

} // namespace stolmine
