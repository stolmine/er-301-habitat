#pragma once

#include <od/objects/Object.h>
#include <string>

namespace stolmine
{

  class CodescanOsc : public od::Object
  {
  public:
    CodescanOsc();
    virtual ~CodescanOsc();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mVOct{"V/Oct"};
    od::Inlet mSync{"Sync"};
    od::Outlet mOutput{"Out"};
    od::Parameter mScan{"Scan", 0.0f};      // 0-1, position in data
    od::Parameter mFundamental{"Fundamental", 110.0f};
#endif

    void loadData(const char *path);
    const char *getFilePath();
    int getDataSize();

  private:
    unsigned char *mData = nullptr;
    int mDataSize = 0;
    float mPhase = 0.0f;
    bool mSyncWasHigh = false;
    float mDCState = 0.0f; // one-pole DC blocker state
    std::string mFilePath;
  };

} // namespace stolmine
