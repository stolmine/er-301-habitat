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
    od::Inlet mVOct{"V/Oct"};      // transposes BOTH clocks (playable osc / tuned combs)
    od::Inlet mBloom{"Bloom"};     // [0,1] allpass smear in the shared resonance loop
    od::Outlet mOut{"Out"};        // mode-tapped output

    // Discrete toggles (values 1..N, never 0 per feedback_option_vs_parameter).
    od::Option mMode{"Mode", 2};       // 1=LP 2=BP 3=HP 4=Notch 5=AP 6=Hidden
    od::Option mClkSrc{"Clock Src", 1}; // 1=A 2=B 3=Both
    od::Option mAlias{"Aliasing", 1};   // 1=LO 2=HI
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
