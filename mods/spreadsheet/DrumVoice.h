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
    od::Parameter mPunch{"Punch", 0.3f};
    od::Parameter mSweep{"Sweep", 12.0f};
    od::Parameter mSweepTime{"SweepTime", 0.03f};
    od::Parameter mAttack{"Attack", 0.0f};
    od::Parameter mHold{"Hold", 0.0f};
    od::Parameter mDecay{"Decay", 0.2f};
    od::Parameter mClipper{"Clipper", 0.0f};
    od::Parameter mEQ{"EQ", 0.0f};
    od::Parameter mLevel{"Level", 0.8f};
    od::Parameter mMakeup{"Makeup", 0.0f};
    od::Parameter mOctave{"Octave", 0.0f};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
