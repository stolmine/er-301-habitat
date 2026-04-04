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
    od::Parameter mAttack{"Attack", 0.005f};      // seconds
    od::Parameter mDecay{"Decay", 0.050f};       // seconds
#endif

  private:
    // Biquad BPF state
    float mX1 = 0.0f, mX2 = 0.0f;
    float mY1 = 0.0f, mY2 = 0.0f;
    // Envelope state
    float mEnv = 0.0f;
    float mThreshold = 0.0f;
  };

} // namespace stolmine
