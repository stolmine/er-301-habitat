#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class GestureSeq : public od::Object
  {
  public:
    GestureSeq();
    virtual ~GestureSeq();

    void clear();
    int getBufferSeconds();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mRun{"Run"};
    od::Inlet mReset{"Reset"};
    od::Inlet mErase{"Erase"};
    od::Outlet mOut{"Out"};

    od::Parameter mOffset{"Offset", 0.0f};
    od::Option mBufferSize{"Buffer Size", 0}; // 0=5s, 1=10s, 2=20s
    od::Option mWriteActive{"Write Active", 0}; // 0=idle, 1=writing (read-only, for UI)
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
