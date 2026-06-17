// Mirror — aliasing-paradigm complex osc.
//
// Helicase-derived chassis (carrier + modulator + sync). 2x OS layer
// stripped, full shape bank replaced with a 3-shape source morph,
// Helicase's external sync removed (mod-phase wrap = internal sync
// edge), feedback replaced with Fold-as-outlet for self-patching.
// MirrorBlock between shaper and output is the paradigm-defining
// stage: divider-clocked S&H with NO anti-aliasing on either side.
//
// Sync Threshold knob is cubic-around-integer-ratios, controlling
// the carrier-to-mod LOCK RATIO (1:1, 3:2, 2:1, 5:2, 3:1 anchors).
// Lock plateaus are sticky, chaos transitions are smooth.
//
// Phase 2 = mono. Stereo extension (Phase 3) duplicates the carrier
// pipeline with sync-threshold-derived phase offset.
//
// 5 outlets (mono phase): Out, Clean (bandlimited reference), Fold
// (alias residual), Sync (gate on internal sync edges), Mod (raw
// modulator audio). 2 inlets: V/Oct, FM. No external sync input.
//
// See planning/mirror-unit-design.md for the full architecture.

#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <math.h>

namespace stolmine
{

  class Mirror : public od::Object
  {
  public:
    Mirror();
    virtual ~Mirror();

    // SWIG-visible
    float getOutputSample(int idx);
    float getModulatorSample(int idx);
    float getCarrierPhase();
    float getModPhase();
    float getLockRatio();
    int getMirrorDivisor();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mVOct{"V/Oct"};
    od::Inlet mFM{"FM"};

    od::Outlet mOut{"Out"};
    od::Outlet mClean{"Clean"};
    od::Outlet mDrive{"Drive"};   // Mirror pre-sat output (tanh-driven, pre-S&H)
    od::Outlet mHeldOut{"Held"};  // Mirror post-quantize value (stair-step, pre-reconstruction)
    od::Outlet mFold{"Fold"};
    od::Outlet mSync{"Sync"};
    od::Outlet mModOut{"Mod"};

    od::Parameter mFundamental{"Fundamental", 110.0f};
    od::Parameter mFine{"Fine", 0.0f};
    od::Parameter mShape{"Shape", 0.13f};       // 0..1 -> wavetable frame 0..15. default ~frame 2 (symmetric triangle)
    od::Parameter mFormant{"Formant", 110.0f};  // envelope rate in Hz at V/Oct = 0 (FIXED tracking)
    od::Parameter mModDepth{"ModDepth", 0.5f};
    od::Parameter mSyncThreshold{"SyncThreshold", 0.0f};
    od::Parameter mMirror{"Mirror", 0.0f};
    od::Parameter mFeedback{"Feedback", 0.0f}; // Mirror -> envelope phase (v1: single destination)
    od::Parameter mLevel{"Level", 0.5f};

    od::Option mMirrorReset{"MirrorReset", 1}; // 1 = ON (locked, default), 2 = OFF (free)
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
