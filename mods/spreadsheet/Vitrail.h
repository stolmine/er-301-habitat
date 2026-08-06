#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  // Vitrail - dual switched-capacitor character filter.
  //
  // Systemic port of the profiling POC v5 (planning/refs/compound-dsp-voice/):
  // two switched-capacitor cores, each on its OWN slowly-drifting clock, with a
  // shared resonance loop when both clocks run. The character (switched-cap
  // aliasing, clock combs, breathing self-oscillation, mode reshaping) EMERGES
  // from the mechanism rather than being tabulated. See planning/vitrail-unit.md
  // and coverage-and-gaps.md for the honest confidence map.
  class Vitrail : public od::Object
  {
  public:
    Vitrail();
    virtual ~Vitrail();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mCutA{"Cutoff A"};   // [0,1] -> exp cutoff Hz -> clock A = cutoff * 25
    od::Inlet mCutB{"Cutoff B"};   // [0,1] -> clock B (the interference/comb partner)
    od::Inlet mRes{"Resonance"};   // [0,1] -> Q law + shared-loop gain (self-osc)
    od::Inlet mGain{"Gain"};       // input drive into the softclip
    od::Outlet mOut{"Out"};        // routed output

    // Two filters are ALWAYS in the path. Routing picks each filter's tap type and
    // whether they cascade (A>B) or sum (A+B); Clock Src picks which clock tunes them.
    // ModeSelector-driven Parameters (right tool for >2 values, feedback_option_vs_parameter).
    //   Routing is NORMALIZED 0-1 (so a 0-1 CV sweeps the whole list); process()
    //   scales it to index 0..49: [0,25) series a>b, [25,50) parallel a+b;
    //   within, idx=a*5+b, type a,b in {0=LP,1=BP,2=HP,3=AP,4=Notch}.
    od::Parameter mRouting{"Routing", 0.0f};
    od::Parameter mClkSrc{"Clock Src", 0.0f};  // 0=A 1=B 2=Both
    od::Option mAlias{"Aliasing", 1};          // 1=LO 2=HI
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
