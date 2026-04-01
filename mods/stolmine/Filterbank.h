#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>

namespace stolmine
{

  static const int kMaxBands = 16;
  static const int kMaxScaleDegrees = 24;

  enum FilterType
  {
    FTYPE_PEAK = 0,
    FTYPE_BPF,
    FTYPE_LP,
    FTYPE_RESON,
    FTYPE_COUNT
  };

  enum ScaleType
  {
    SCALE_CHROMATIC = 0,
    SCALE_MAJOR,
    SCALE_NATURAL_MINOR,
    SCALE_HARMONIC_MINOR,
    SCALE_MAJOR_PENT,
    SCALE_MINOR_PENT,
    SCALE_WHOLE_TONE,
    SCALE_DORIAN,
    SCALE_PHRYGIAN,
    SCALE_LYDIAN,
    SCALE_MIXOLYDIAN,
    SCALE_LOCRIAN,
    SCALE_CUSTOM,
    SCALE_COUNT
  };

  class Filterbank : public od::Object
  {
  public:
    Filterbank();
    virtual ~Filterbank();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Outlet mOut{"Out"};

    od::Parameter mMix{"Mix", 1.0f};
    od::Parameter mMacroQ{"MacroQ", 0.5f};
    od::Parameter mBandCount{"BandCount", 8.0f};
    od::Parameter mScale{"Scale", 0.0f};
    od::Parameter mRotate{"Rotate", 0.0f};
    od::Parameter mSkew{"Skew", 0.5f};
    od::Parameter mSlew{"Slew", 0.0f};
    od::Parameter mInputLevel{"InputLevel", 1.0f};
    od::Parameter mOutputLevel{"OutputLevel", 1.0f};
    od::Parameter mTanhAmt{"TanhAmt", 0.0f};

    // Edit buffer
    od::Parameter mEditFreq{"EditFreq", 440.0f};
    od::Parameter mEditGain{"EditGain", 1.0f};
    od::Parameter mEditType{"EditType", 0.0f};
#endif

    // SWIG-visible: band data accessors (freq in Hz)
    float getBandFreq(int i);
    void setBandFreq(int i, float hz);
    float getBandGain(int i);
    void setBandGain(int i, float v);
    int getBandType(int i);
    void setBandType(int i, int v);

    void loadBand(int i);
    void storeBand(int i);

    // SWIG-visible: UI state
    int getBandCount();
    int getScaleCount() { return (int)SCALE_COUNT; }

    // Custom scale loading (called from Lua for Scala files)
    void beginCustomScale();
    void addCustomDegree(float cents);
    void endCustomScale(); // sets scale to custom and redistributes

    // For overview graphic
    float evaluateResponse(float normalizedFreq);

#ifndef SWIGLUA
  private:
    struct Internal;
    Internal *mpInternal;

    int mCachedBandCount = 8;

    // Change detection for auto-redistribution
    int mLastScale = -1;
    int mLastRotate = 0;
    int mLastBandCount = 8;
    float mLastSkew = 0.5f;

    void updateFilterCoefficients();
    void distributeFrequencies();
    void checkDistributionDirty();
#endif
  };

} // namespace stolmine
