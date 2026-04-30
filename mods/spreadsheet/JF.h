#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  // JF — hex-voiced harmonically-coupled slope-engine voice. Clean-room
  // implementation from a public technical map. v1 ships the 6 default
  // base cells (Range x Mode = Sound/Shape x Transient/Sustain/Cycle).
  // RUN-mode personalities (SHIFT, STRATA, VOLLEY, SPILL, PLUME, FLOOM)
  // and Just-Type style poly/Geode are deferred (v2 / v3).
  //
  // Multi-output: 7 sub-outs. Sub-out 1 = primary = MIX. Sub-outs 2-7 =
  // per-voice IDENTITY (1N) through 6N. See planning/just-friends.md and
  // planning/jf-initial-pass.md for the design + phase plan.
  //
  // Phase 1 stub: declares the 7 outlets and writes silence. No DSP yet.
  class JF : public od::Object
  {
  public:
    JF();
    virtual ~JF();

#ifndef SWIGLUA
    virtual void process();

    // Inlets — populated in later phases. Phase 1 declares only what the
    // skeleton wires from Lua.
    od::Inlet mVOct{"V/Oct"};
    od::Inlet mFM{"FM In"};

    // 7 sub-outlets. Vanilla auto-wires Out1 (MIX) to chain L.
    od::Outlet mMix{"Out1"};   // sub-out 1 — MIX (primary)
    od::Outlet mOut1N{"Out2"}; // sub-out 2 — IDENTITY / 1N
    od::Outlet mOut2N{"Out3"}; // sub-out 3 — 2N
    od::Outlet mOut3N{"Out4"}; // sub-out 4 — 3N
    od::Outlet mOut4N{"Out5"}; // sub-out 5 — 4N
    od::Outlet mOut5N{"Out6"}; // sub-out 6 — 5N
    od::Outlet mOut6N{"Out7"}; // sub-out 7 — 6N

    // Phase 1 parameters — placeholder set so onLoadGraph can wire faders
    // in v1 layout pass without DSP wired up yet.
    od::Parameter mTimeBias{"TimeBias", 0.0f};
    od::Parameter mIntone{"Intone", 0.0f};
    od::Parameter mRamp{"Ramp", 0.0f};
    od::Parameter mCurve{"Curve", 0.0f};
    od::Parameter mFmDepth{"FmDepth", 0.0f};
    od::Parameter mOut{"Out", 0.0f};        // OUT crossfader 0..6
    od::Option mRange{"Range", 1};          // 1 = Shape, 2 = Sound
    od::Option mMode{"Mode", 1};            // 1 = Transient, 2 = Sustain, 3 = Cycle
    od::Option mOutMode{"OutMode", 1};      // 1 = smooth, 2 = snap
#endif
  };

} // namespace stolmine
