#pragma once

#include <od/objects/Object.h>
#include <string>

namespace stolmine
{

  static const int kMaxFIRTaps = 64;

  class CodescanFilter : public od::Object
  {
  public:
    CodescanFilter();
    virtual ~CodescanFilter();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mInput{"In"};
    od::Outlet mOutput{"Out"};
    od::Parameter mScan{"Scan", 0.0f};     // 0-1, position in data
    od::Parameter mTaps{"Taps", 32.0f};    // 4-64
    od::Parameter mMix{"Mix", 0.5f};       // 0 dry, 1 wet
#endif

    void loadData(const char *path);
    const char *getFilePath();
    int getDataSize();

  private:
    unsigned char *mData = nullptr;
    int mDataSize = 0;
    float mDelayLine[kMaxFIRTaps];
    int mWriteIdx = 0;
    float mDCState = 0.0f; // one-pole DC blocker state
    std::string mFilePath;
  };

} // namespace stolmine
