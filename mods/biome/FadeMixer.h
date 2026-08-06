#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  // FadeMixer - crossfade across N inputs (4/6/8) with a single Fade control.
  // Fade sweeps a triangular equal-power window across the ACTIVE inputs; the
  // active count is set by the Inputs parameter (the Lua unit fixes it to 4/6/8
  // and wires that many branches). Extra inlets stay silent.
  class FadeMixer : public od::Object
  {
  public:
    FadeMixer();
    virtual ~FadeMixer();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mIn1{"In1"};
    od::Inlet mIn2{"In2"};
    od::Inlet mIn3{"In3"};
    od::Inlet mIn4{"In4"};
    od::Inlet mIn5{"In5"};
    od::Inlet mIn6{"In6"};
    od::Inlet mIn7{"In7"};
    od::Inlet mIn8{"In8"};
    od::Outlet mOutput{"Out"};
    od::Parameter mFade{"Fade", 0.0f};     // 0-1, position across the active inputs
    od::Parameter mLevel{"Level", 1.0f};   // output level
    od::Parameter mInputs{"Inputs", 4.0f}; // active input count (4/6/8)
    od::Option mMode{"Mode", 1};           // 1 = Smooth (crossfade), 2 = Snap (N->1 switch)
#endif

  private:
    // Live per-channel gains. Class members, not stack locals: they carry the
    // declick ramp's state between blocks, and stack float arrays are the
    // Cortex-A8 NEON alignment-hint trap (feedback_neon_intrinsics_drumvoice).
    float mG[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  };

} // namespace stolmine
