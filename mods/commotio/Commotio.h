#pragma once

#include <od/objects/Object.h>

namespace commotio_unit
{

  class Commotio : public od::Object
  {
  public:
    Commotio();
    virtual ~Commotio();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mGate{"Gate"};
    od::Outlet mOut{"Out"};

    od::Parameter mBowLevel{"BowLevel", 0.0f};
    od::Parameter mBowTimbre{"BowTimbre", 0.5f};
    od::Parameter mBlowLevel{"BlowLevel", 0.0f};
    od::Parameter mBlowTimbre{"BlowTimbre", 0.5f};
    od::Parameter mBlowMeta{"BlowMeta", 0.5f};
    od::Parameter mStrikeLevel{"StrikeLevel", 0.5f};
    od::Parameter mStrikeTimbre{"StrikeTimbre", 0.5f};
    od::Parameter mStrikeMeta{"StrikeMeta", 0.5f};
    od::Parameter mEnvShape{"EnvShape", 0.5f};
    od::Parameter mDamping{"Damping", 0.5f};
    od::Parameter mBrightness{"Brightness", 0.5f};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace commotio_unit
