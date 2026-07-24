#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  // Tessera - analog-style "building block" drum voice.
  //
  // Built from the profiled Modbap Trinity "BLOCK" algorithm (measured 2026-07-19,
  // ~/repos/trinity-midi-harness/findings-block.md). Measured laws:
  //   - Character = waveshape morph: triangle (0) -> sine (~0.4) -> triangle FOLDING
  //     (0.5..1, a wavefolder that re-adds harmonics).
  //   - Shape = a 2nd oscillator overlay (808/909 dual-osc) with a dampened decay.
  //   - Grit = noise blend that ramps toward "just noise", AND shortens the amp
  //     envelope past ~0.75 (808-snare snappiness).
  //   - Pitch env: Sweep depth, Time rate (Time = pitch-sweep rate; measured - the
  //     manual's BLOCK Time/Decay text is transposed). Amp env: Decay + Hold.
  // Emergent-by-construction; generic name (no branding). Distinct engine from Ferrum.
  class Tessera : public od::Object
  {
  public:
    Tessera();
    virtual ~Tessera();

    float getEnvLevel();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mTrigger{"Trigger"};
    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};

    od::Parameter mF0{"Fundamental", 60.0f};     // direct fundamental Hz (+ V/oct)
    od::Parameter mCharacter{"Character", 0.2f}; // triangle -> sine -> fold
    od::Parameter mShape{"Shape", 0.0f};         // 2nd oscillator overlay
    od::Parameter mGrit{"Grit", 0.0f};           // noise blend + env shortening
    od::Parameter mSweep{"Sweep", 0.3f};         // pitch-env depth
    od::Parameter mTime{"Time", 0.3f};           // pitch-env rate
    od::Parameter mHold{"Hold", 0.1f};           // amp-env hold plateau
    od::Parameter mDecay{"Decay", 0.4f};         // amp-env decay
    od::Parameter mClipper{"Clipper", 0.378f};  // 48/127 = the corpus operating point
    od::Parameter mLevel{"Level", 0.8f};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
