#pragma once

#include <od/objects/Object.h>
#include <od/config.h>

namespace stolmine
{

  class MultibandCompressor : public od::Object
  {
  public:
    MultibandCompressor();
    virtual ~MultibandCompressor();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mSidechain{"Sidechain"};
    od::Outlet mOut{"Out"};

    // Global
    od::Parameter mDrive{"Drive", 1.0f};
    od::Parameter mToneAmount{"ToneAmount", 0.0f};
    od::Parameter mToneFreq{"ToneFreq", 800.0f};
    od::Parameter mSkew{"Skew", 0.0f};
    od::Parameter mMix{"Mix", 1.0f};
    od::Parameter mOutputLevel{"OutputLevel", 1.0f};
    od::Parameter mInputGain{"InputGain", 1.0f};

    // Per-band (x3): threshold, ratio, speed, attack, release, weight
    od::Parameter mBandThreshold0{"BandThreshold0", 0.5f};
    od::Parameter mBandThreshold1{"BandThreshold1", 0.5f};
    od::Parameter mBandThreshold2{"BandThreshold2", 0.5f};
    od::Parameter mBandRatio0{"BandRatio0", 2.0f};
    od::Parameter mBandRatio1{"BandRatio1", 2.0f};
    od::Parameter mBandRatio2{"BandRatio2", 2.0f};
    od::Parameter mBandSpeed0{"BandSpeed0", 0.3f};
    od::Parameter mBandSpeed1{"BandSpeed1", 0.3f};
    od::Parameter mBandSpeed2{"BandSpeed2", 0.3f};
    od::Parameter mBandAttack0{"BandAttack0", 0.001f};
    od::Parameter mBandAttack1{"BandAttack1", 0.001f};
    od::Parameter mBandAttack2{"BandAttack2", 0.001f};
    od::Parameter mBandRelease0{"BandRelease0", 0.05f};
    od::Parameter mBandRelease1{"BandRelease1", 0.05f};
    od::Parameter mBandRelease2{"BandRelease2", 0.05f};
    od::Parameter mBandWeight0{"BandWeight0", 1.0f};
    od::Parameter mBandWeight1{"BandWeight1", 1.0f};
    od::Parameter mBandWeight2{"BandWeight2", 1.0f};
    od::Option mAutoMakeup{"AutoMakeup", 2};       // 1=on, 2=off
    od::Option mEnableSidechain{"EnableSidechain", 2}; // 1=on, 2=off
#endif

    // SWIG-visible
    float getCrossoverFreq(int band);
    float getBandGainReduction(int band);
    float getFFTRms(int bin);
    float getBandLevel(int band);
    int getCrossoverBin(int band);
    void setBandBias(int band, int param, od::Parameter *p);

    bool isAutoMakeupEnabled();
    void toggleAutoMakeup();
    bool isSidechainEnabled();
    void toggleSidechainEnabled();

  private:
    struct Internal;
    Internal *mpInternal;

    // Bias refs: [band][param]: 0=threshold, 1=ratio, 2=speed, 3=attack, 4=release, 5=weight
    static const int kCompBiasCount = 6;
    od::Parameter *mBandBias[3][6];

    float mLastWeight[3];
    float mLastSkew;

#ifndef SWIGLUA
    void recomputeCrossovers();
#endif
  };

} // namespace stolmine
