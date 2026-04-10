#pragma once

#include <od/objects/Object.h>
#include <od/config.h>

namespace stages { class VariableShapeOscillator; }
namespace plaits { class DecayEnvelope; }

namespace stolmine
{

  class VarishapeVoice : public od::Object
  {
  public:
    VarishapeVoice();
    virtual ~VarishapeVoice();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mVOct{"V/Oct"};
    od::Inlet mSync{"Sync"};
    od::Inlet mGate{"Gate"};
    od::Outlet mOutput{"Out"};
    od::Parameter mShape{"Shape", 0.0f};
    od::Parameter mFundamental{"Fundamental", 110.0f};
    od::Parameter mDecay{"Decay", 0.5f};
#endif

  private:
    stages::VariableShapeOscillator *mpOsc;
    plaits::DecayEnvelope *mpEnv;
    float *mpWorkBuffer;
    bool mGateWasHigh;
  };

} // namespace stolmine
