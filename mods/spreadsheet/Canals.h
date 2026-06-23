#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class Canals : public od::Object
  {
  public:
    Canals();
    virtual ~Canals();

    // SWIG-visible — routing viz support.
    // block index: 0=LOW, 1=CENTRE, 2=HIGH.
    float getBlockInputSample(int block, int idx);
    bool  isBlockUsingAll(int block);

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};         // ALL signal (main input)
    od::Inlet mLowIn{"Low In"};   // per-block LOW input; overrides ALL when patched
    od::Inlet mCentreIn{"Centre In"}; // per-block CTR input; overrides ALL when patched
    od::Inlet mHighIn{"High In"}; // per-block HIGH input; overrides ALL when patched
    od::Inlet mVOct{"V/Oct"};
    // Span + Quality are audio-rate Inlets (per-sample read, fed by a
    // Lua GainBias->connect), NOT block-rate Parameters. This is what
    // lets Canals take audio-rate Span/Quality modulation cleanly
    // instead of stair-stepping ("flapping"). Mirrors the native Sine
    // Osc, where every modulatable input is an Inlet. Fundamental stays
    // a Parameter knob — V/Oct already carries audio-rate freq FM.
    // See planning/canals-audio-rate-mod.md Phase 5.
    od::Inlet mSpan{"Span"};
    od::Inlet mQuality{"Quality"};
    od::Outlet mOut{"Out"};         // fader-selected mix (sub-out 1, chain auto-wire)
    od::Outlet mOutLow{"Low"};      // parallel LOW band tap
    od::Outlet mOutCentre{"Centre"}; // parallel CENTRE band tap
    od::Outlet mOutHigh{"High"};    // parallel HIGH band tap
    od::Parameter mFundamental{"Fundamental", 0.0f};
    od::Parameter mOutput{"Output", 0.0f};
    od::Parameter mMode{"Mode", 0.0f}; // 0=crossover, 1=formant
    // Per-block input routing (set from Lua side via branch-state polling).
    // 1 = unpatched (use ALL feed); 2 = patched (use per-block In).
    // Option-value convention per feedback_option_vs_parameter (never 0).
    od::Option mAllEnabled{"AllEnabled", 1};       // 1=ON (default), 2=OFF
    od::Option mLowPatched{"LowPatched", 1};       // 1=unpatched, 2=patched
    od::Option mCentrePatched{"CentrePatched", 1}; // 1=unpatched, 2=patched
    od::Option mHighPatched{"HighPatched", 1};     // 1=unpatched, 2=patched
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
