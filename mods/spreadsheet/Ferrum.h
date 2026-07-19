#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  // Ferrum - 2-operator FM drum voice.
  //
  // Built from the profiled Modbap Trinity "NEON/FM" algorithm (measured
  // 2026-07-19, ~/repos/trinity-midi-harness/findings.md). The measured laws:
  //   - 2-op FM: carrier at pitch f, modulator at ratio*f (Ratio = quantized
  //     integer harmonic steps).
  //   - Index (mod depth) brightens monotonically, becoming operator feedback at
  //     the top ("feedback regime").
  //   - Grit: feedback boost, then crossfade to broadband noise past ~0.55.
  //   - Pitch envelope: attack f*(1+depth), exp decay to base (Sweep=depth, Time=rate).
  //   - Amp envelope: instant attack, Hold plateau, exp Decay release.
  // Emergent-by-construction; no per-CC lookup tables. Generic name (no branding).
  class Ferrum : public od::Object
  {
  public:
    Ferrum();
    virtual ~Ferrum();

    float getEnvLevel();   // SWIG-visible for a future viz

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mTrigger{"Trigger"};
    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};

    // Controls match the Trinity FM voice basics (Character = mod depth -> feedback,
    // Shape = FM ratio), not broken-out index/ratio.
    od::Parameter mPitch{"Pitch", 0.4f};       // base tuning: ~12 Hz .. 650 Hz (~5.75 oct)
    od::Parameter mCharacter{"Character", 0.5f}; // FM mod depth -> operator feedback at top
    od::Parameter mShape{"Shape", 0.45f};      // continuous FM ratio (~1 .. 7.5)
    od::Parameter mGrit{"Grit", 0.0f};         // feedback boost -> noise
    od::Parameter mSweep{"Sweep", 0.3f};       // pitch-env depth
    od::Parameter mTime{"Time", 0.3f};         // pitch-env rate
    od::Parameter mHold{"Hold", 0.1f};         // amp-env hold plateau
    od::Parameter mDecay{"Decay", 0.4f};       // amp-env decay
    od::Parameter mLevel{"Level", 0.8f};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
