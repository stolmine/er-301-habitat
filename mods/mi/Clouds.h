#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>

namespace clouds_unit
{

  class Clouds : public od::Object
  {
  public:
    Clouds();
    virtual ~Clouds();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mInL{"In L"};
    od::Inlet mInR{"In R"};
    od::Inlet mTrigger{"Trigger"};
    od::Inlet mFreeze{"Freeze"};
    od::Outlet mOutL{"Out L"};
    od::Outlet mOutR{"Out R"};

    od::Parameter mPosition{"Position", 0.5f};
    od::Parameter mSize{"Size", 0.5f};
    od::Parameter mPitch{"Pitch", 0.0f};
    od::Parameter mDensity{"Density", 0.5f};
    od::Parameter mTexture{"Texture", 0.5f};
    od::Parameter mDryWet{"Dry/Wet", 0.5f};
    od::Parameter mFeedback{"Feedback", 0.0f};
    od::Parameter mSpread{"Spread", 0.5f};
    od::Parameter mMode{"Mode", 0.0f};  // 0=granular, 1=stretch, 2=delay, 3=spectral
    od::Option mQuality{"Quality", 0}; // 0=16bit stereo, 1=16bit mono
    od::Option mPreamp{"Preamp", 0};   // 0=unity, 1=x2, 2=x4
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace clouds_unit
