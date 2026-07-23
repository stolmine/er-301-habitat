#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <math.h>

namespace stolmine
{

  // DrumVoice ("Ngoma") - macro drum voice.
  //
  // Engine: the modal lattice from Tessera.cpp (measured Trinity BLOCK laws,
  // ~/repos/trinity-midi-harness/analysis-modemap.md), transplanted verbatim
  // behind Ngoma's 14-parameter control surface and output chain (variable
  // clipper, EQ, compressor, level). See planning/ngoma-tessera-integration.md
  // section 4 for the mapping. Tessera.cpp/.h stay frozen as the reference
  // model; do not port changes back from here.
  class DrumVoice : public od::Object
  {
  public:
    DrumVoice();
    virtual ~DrumVoice();

    float getCharacter();
    float getShape();
    float getGrit();
    float getEnvLevel();
    bool getGateState();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mTrigger{"Trigger"};
    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};

    od::Parameter mCharacter{"Character", 0.5f};
    od::Parameter mShape{"Shape", 0.0f};
    od::Parameter mGrit{"Grit", 0.0f};
    od::Parameter mPunch{"Punch", 0.4f};
    od::Parameter mSweep{"Sweep", 18.0f};
    od::Parameter mSweepTime{"SweepTime", 0.04f};
    od::Parameter mAttack{"Attack", 0.0f};
    od::Parameter mHold{"Hold", 0.0f};
    od::Parameter mDecay{"Decay", 0.25f};
    od::Parameter mClipper{"Clipper", 0.0f};  // default = CLEAN (bottom of throw); top = corpus-point heft (P4/P0-shape)
    od::Parameter mEQ{"EQ", 0.0f};
    od::Parameter mLevel{"Level", 0.8f};
    od::Parameter mCompAmt{"CompAmt", 0.0f};
    od::Parameter mOctave{"Octave", 0.0f};
#endif

  private:
    // Modal engine state (phase/env/mfreq/mdecay/ramp[16] etc.) lives in the
    // heap-allocated Internal below (NEON-legal storage for the P5 pass).
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
