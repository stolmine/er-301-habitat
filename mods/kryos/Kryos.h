#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>

namespace kryos
{

  static const int NUM_BANDS = 12;

  class Kryos : public od::Object
  {
  public:
    Kryos();
    virtual ~Kryos();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mFreeze{"Freeze"};
    od::Outlet mOut{"Out"};

    od::Parameter mPosition{"Position", 0.5f};
    od::Parameter mPitch{"Pitch", 0.0f};
    od::Parameter mSize{"Size", 0.5f};
    od::Parameter mTexture{"Texture", 0.0f};
    od::Parameter mDecay{"Decay", 0.5f};
    od::Parameter mMix{"Mix", 0.5f};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace kryos
