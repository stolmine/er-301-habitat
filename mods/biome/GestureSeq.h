#pragma once

#include <od/objects/heads/TapeHead.h>

namespace stolmine
{

  class GestureSeq : public od::TapeHead
  {
  public:
    GestureSeq();
    virtual ~GestureSeq();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mRun{"Run"};
    od::Inlet mReset{"Reset"};
    od::Inlet mErase{"Erase"};
    od::Outlet mOut{"Out"};

    od::Parameter mOffset{"Offset", 0.0f};
    od::Parameter mSlew{"Slew", 0.0f};
    od::Option mWriteActive{"Write Active", 0};
    od::Option mSensitivity{"Sensitivity", 1}; // 0=low, 1=medium, 2=high
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
