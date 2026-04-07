#pragma once

#include <od/objects/Object.h>
#include <od/config.h>

namespace stolmine
{

  class MultibandSaturator : public od::Object
  {
  public:
    MultibandSaturator();
    virtual ~MultibandSaturator();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Outlet mOut{"Out"};

    // Global
    od::Parameter mDrive{"Drive", 1.0f};
    od::Parameter mToneAmount{"ToneAmount", 0.0f};
    od::Parameter mToneFreq{"ToneFreq", 800.0f};
    od::Parameter mSkew{"Skew", 1.0f};
    od::Parameter mMix{"Mix", 1.0f};
    od::Parameter mOutputLevel{"OutputLevel", 1.0f};
    od::Parameter mCompressAmt{"CompressAmt", 0.0f};
    od::Parameter mTanhAmt{"TanhAmt", 0.0f};

    // Per-band (x3)
    od::Parameter mBandLevel0{"BandLevel0", 1.0f};
    od::Parameter mBandLevel1{"BandLevel1", 1.0f};
    od::Parameter mBandLevel2{"BandLevel2", 1.0f};
    od::Parameter mBandAmount0{"BandAmount0", 0.5f};
    od::Parameter mBandAmount1{"BandAmount1", 0.5f};
    od::Parameter mBandAmount2{"BandAmount2", 0.5f};
    od::Parameter mBandBias0{"BandBias0", 0.0f};
    od::Parameter mBandBias1{"BandBias1", 0.0f};
    od::Parameter mBandBias2{"BandBias2", 0.0f};
    od::Parameter mBandType0{"BandType0", 0.0f};
    od::Parameter mBandType1{"BandType1", 0.0f};
    od::Parameter mBandType2{"BandType2", 0.0f};
    od::Parameter mBandWeight0{"BandWeight0", 1.0f};
    od::Parameter mBandWeight1{"BandWeight1", 1.0f};
    od::Parameter mBandWeight2{"BandWeight2", 1.0f};
    od::Parameter mBandFilterFreq0{"BandFilterFreq0", 1000.0f};
    od::Parameter mBandFilterFreq1{"BandFilterFreq1", 1000.0f};
    od::Parameter mBandFilterFreq2{"BandFilterFreq2", 1000.0f};
    od::Parameter mBandFilterMorph0{"BandFilterMorph0", 0.0f};
    od::Parameter mBandFilterMorph1{"BandFilterMorph1", 0.0f};
    od::Parameter mBandFilterMorph2{"BandFilterMorph2", 0.0f};
    od::Parameter mBandFilterQ0{"BandFilterQ0", 0.5f};
    od::Parameter mBandFilterQ1{"BandFilterQ1", 0.5f};
    od::Parameter mBandFilterQ2{"BandFilterQ2", 0.5f};
    od::Parameter mBandMute0{"BandMute0", 0.0f};
    od::Parameter mBandMute1{"BandMute1", 0.0f};
    od::Parameter mBandMute2{"BandMute2", 0.0f};
    od::Parameter mScHpf{"ScHpf", 0.0f};
#endif

    // SWIG-visible
    float getCrossoverFreq(int band);
    float getBandEnergy(int band);
    float getFFTPeak(int bin);
    float getFFTRms(int bin);
    float getBandLevel(int band);
    bool getBandMuted(int band);
    int getCrossoverBin(int band);
    void setBandBias(int band, int param, od::Parameter *p);
    void setBandLevelBias(int band, od::Parameter *p);

  private:
    struct Internal;
    Internal *mpInternal;

    // Bias refs for per-band params (read directly from UI)
    // [band][param]: 0=amount, 1=bias, 2=type, 3=weight, 4=filterFreq, 5=filterMorph, 6=filterQ
    static const int kBiasCount = 7;
    od::Parameter *mBandBias[3][7];
    od::Parameter *mBandLevelBias[3];

    // Dirty-check
    float mLastWeight[3];
    float mLastSkew;

#ifndef SWIGLUA
    void recomputeCrossovers();
#endif
  };

} // namespace stolmine
