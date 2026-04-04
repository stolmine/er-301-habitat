#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class SpectralFollower : public od::Object
  {
  public:
    SpectralFollower();
    virtual ~SpectralFollower();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mInput{"In"};
    od::Outlet mOutput{"Out"};
    od::Parameter mFreq{"Freq", 1000.0f};      // Center frequency Hz
    od::Parameter mBandwidth{"Bandwidth", 1.0f}; // Octaves
    od::Parameter mAttack{"Attack", 5.0f};       // ms
    od::Parameter mDecay{"Decay", 50.0f};        // ms
#endif

  private:
    // Biquad BPF state
    float mX1 = 0.0f, mX2 = 0.0f;
    float mY1 = 0.0f, mY2 = 0.0f;
    // Envelope state
    float mEnv = 0.0f;
  };

} // namespace stolmine
