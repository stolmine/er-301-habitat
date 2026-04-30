#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>

namespace stratos
{

  class Stratos : public od::Object
  {
  public:
    Stratos();
    virtual ~Stratos();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mInL{"In L"};
    od::Inlet mInR{"In R"};
    od::Outlet mOutL{"Out L"};
    od::Outlet mOutR{"Out R"};

    od::Parameter mAmount{"Amount", 0.54f};
    od::Parameter mTime{"Time", 0.98f};
    od::Parameter mDiffusion{"Diffusion", 0.7f};
    od::Parameter mDamping{"Damping", 0.6f};
    od::Parameter mGain{"Gain", 0.2f};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stratos
