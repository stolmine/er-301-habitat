// Chime -- coupled pulse-excited resonator bank for ER-301
//
// Inspired by Ciat-Lonbarde Plumbutter's Gongs kernel, but not a port.
// What makes the unit distinctive vs a plain parallel filter bank:
//   * Cross-coupling: each resonator's previous-sample output feeds (weakly)
//     into its two nearest neighbours' inputs on the next sample.
//   * Amplitude-modulated detune: each resonator's effective cutoff is nudged
//     by the envelope of its loudest neighbour, producing the "breathing" /
//     "stability in chaos" behaviour Blasser's designs are known for.
// See docs/microsound-research.md for design rationale.

#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>

namespace stolmine
{
  static const int kChimeMaxBands = 8;
  static const int kChimeMaxScaleDegrees = 128;
  static const int kChimeMaxCustomScales = 16;

  enum ChimeScaleType
  {
    CHIME_SCALE_CHROMATIC = 0,
    CHIME_SCALE_MAJOR,
    CHIME_SCALE_MINOR_PENT,
    CHIME_SCALE_MAJOR_PENT,
    CHIME_SCALE_WHOLE_TONE,
    CHIME_SCALE_HARMONIC,
    CHIME_SCALE_PELOG,
    CHIME_SCALE_SLENDRO,
    CHIME_SCALE_COUNT
  };

  class Chime : public od::Object
  {
  public:
    Chime();
    virtual ~Chime();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mTrigger{"Trigger"};
    od::Outlet mOut{"Out"};

    od::Parameter mBandCount{"BandCount", 6.0f};
    od::Parameter mScale{"Scale", 0.0f};
    od::Parameter mRotate{"Rotate", 0.0f};
    od::Parameter mQ{"Q", 0.6f};            // 0..1 maps to Q 10..120
    od::Parameter mCouple{"Couple", 0.3f};  // 0..1 cross-coupling strength
    od::Parameter mDetune{"Detune", 0.2f};  // 0..1 amplitude-modulated detune
    od::Parameter mDrive{"Drive", 0.0f};    // 0..1 tanh wet on summed output
    od::Parameter mLevel{"Level", 1.0f};    // 0..2 output gain
    od::Parameter mInputLevel{"InputLevel", 1.0f};   // 0..2 audio-through
    od::Parameter mImpulseGain{"ImpulseGain", 1.0f}; // 0..2 impulse amplitude
    od::Parameter mSpread{"Spread", 0.0f};           // 0..1 round-robin weight
#endif

    // SWIG-visible viz accessors
    int getBandCount();
    float getBandFreq(int i);
    float getBandEnergy(int i);
    float getCouple();

    // Scale table metadata for Lua
    int getScaleCount() { return (int)CHIME_SCALE_COUNT; }

#ifndef SWIGLUA
  private:
    struct Internal;
    Internal *mpInternal;

    int mCachedBandCount = 6;

    // Dirty-check caches
    int mLastScale = -1;
    int mLastRotate = 0;
    int mLastBandCount = -1;

    // Round-robin cursor for Spread
    int mTriggerIndex = 0;

    // Rising-edge state
    bool mTriggerWasHigh = false;

    void distributeFrequencies();
    void checkDistributionDirty();
    void updateFilterCoefficients(float qNorm, float detuneBudget);
#endif
  };

} // namespace stolmine
