#pragma once

// Visadhara — clean-room percussion macro voice based on the public BIA
// technical manual (manuals.noiseengineering.us/bia). Six tonal + one noise
// oscillator across three modes (Skin / Liquid / Metal). Phase 1: Skin mode
// only — 6-voice NEON additive with Spread (harmonic↔prime overtones),
// Harmonic (per-voice decay+amp scaling), Morph (sin→tri→saw→sq blend), AR
// envelope (no sustain), global Decay + Level.
//
// Architecture: see planning/bia-clone-scoping.md.
// Implementation plan: see planning/visadhara-initial-pass.md.

#include <od/objects/Object.h>
#include <od/config.h>

namespace stolmine
{

  class Visadhara : public od::Object
  {
  public:
    Visadhara();
    virtual ~Visadhara();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mTrigger{"Trigger"};
    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};

    // Phase 1 controls. Attack / Fold / Mode / ModeSnap added in Phase 2-3.
    od::Parameter mHarmonic{"Harmonic", 0.5f};   // 0..1
    od::Parameter mSpread{"Spread", 0.0f};       // 0..1 (0 = harmonic, 1 = prime)
    od::Parameter mMorph{"Morph", 0.0f};         // 0..1 (0=sin, 0.33=tri, 0.67=saw, 1=sq)
    od::Parameter mDecay{"Decay", 0.5f};         // 0..1 → exp decay coefficient
    od::Parameter mLevel{"Level", 0.7f};         // 0..1 output gain
    od::Parameter mPitch{"Pitch", 110.0f};       // base Hz at V/Oct=0

    // Mode + ModeSnap stubbed in Phase 1 (Skin mode treated as default).
    od::Parameter mMode{"Mode", 0.0f};           // 0..2 continuous; Phase 3+ semantics
    od::Option    mModeSnap{"ModeSnap", 1};      // 1 = smooth (default), 2 = snap
    od::Option    mOctave{"Octave", 2};          // 1=Bass(-2), 2=Alto(0), 3=Treble(+2)
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
