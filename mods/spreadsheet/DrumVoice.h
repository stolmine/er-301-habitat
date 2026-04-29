#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <math.h>

namespace stolmine
{

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
    od::Parameter mClipper{"Clipper", 0.0f};
    od::Parameter mEQ{"EQ", 0.0f};
    od::Parameter mLevel{"Level", 0.8f};
    od::Parameter mCompAmt{"CompAmt", 0.0f};
    od::Parameter mOctave{"Octave", 0.0f};
#endif

  private:
    // NEON working set as class members. Lanes:
    //   0 = phaseFm (metallic FM, 2.71x ratio)
    //   1 = phase4  (spacious FM, 2.0x ratio)
    //   2 = phase5  (sub-sine fundamental)
    //   3 = reserved
    float mPhaseBank[4];
    float mIncBank[4];
    float mSineBank[4];

    // Second NEON quad for additive partials. Lane 0 = sub-octave;
    // lanes 1-3 inharmonic membrane modes.
    float mPartialPhases[4];
    float mPartialInc[4];
    float mPartialSines[4];

    // Per-partial decay envelopes (NEON-vectorized decay update).
    float mPartialEnvs[4];
    float mPartialDecayCoeffs[4];

    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
