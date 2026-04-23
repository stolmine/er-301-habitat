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

    void fireRandomize();
    void setTopLevelBias(int which, od::Parameter *param);

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mTrigger{"Trigger"};
    od::Inlet mVOct{"V/Oct"};
    od::Inlet mXformGate{"XformGate"};
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
    od::Parameter mMakeup{"Makeup", 0.0f};
    od::Parameter mOctave{"Octave", 0.0f};
    od::Parameter mXformDepth{"XformDepth", 0.3f};
    od::Parameter mXformSpread{"XformSpread", 0.5f};
#endif

  private:
    void applyRandomize();

    bool mXformGateWasHigh = false;
    bool mManualFire = false;

    // Registered via setTopLevelBias; the randomize function mutates these.
    od::Parameter *mBiasCharacter = nullptr;
    od::Parameter *mBiasShape = nullptr;
    od::Parameter *mBiasGrit = nullptr;
    od::Parameter *mBiasPunch = nullptr;
    od::Parameter *mBiasSweep = nullptr;
    od::Parameter *mBiasSweepTime = nullptr;
    od::Parameter *mBiasAttack = nullptr;
    od::Parameter *mBiasHold = nullptr;
    od::Parameter *mBiasDecay = nullptr;

    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
