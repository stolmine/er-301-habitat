#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  // Moire (prototype pivot) - rain on bells / windchimes: a scattering meso-time network.
  //
  // Sparse stochastic strikes (Density = rain) hit a bank of tuned RESONATORS (bells). Each
  // bell's ring is fed into a shared meso-time delay; taps read it back (tens-to-hundreds of
  // ms later) and re-strike OTHER bells, so one drop ripples through the network as a cascade
  // (Bounce). Petrichor's architecture (multitap delay + resonance + feedback) voiced as
  // rain-struck bells. Bell frequencies still follow the moving lattice f(h,k)=fc*(h+k*r).
  class Moire : public od::Object
  {
  public:
    Moire();
    virtual ~Moire();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mVOct{"V/Oct"};
    od::Inlet mSpread{"Spread"};   // r - lattice/bell tuning; audio-rate, 0..2
    od::Inlet mBody{"Body"};       // bell Q / ring time
    od::Inlet mDensity{"Density"}; // rain strike rate
    od::Inlet mBounce{"Bounce"};   // meso-time cascade feedback (drop ripples through network)
    od::Inlet mTime{"Time"};       // meso-delay window (the space the signal bounces in)
    od::Inlet mDrift{"Drift"};     // per-bell slow frequency wander
    od::Inlet mLock{"Lock"};       // snap bell frequencies toward the harmonic grid
    od::Outlet mOut{"Out"};

    od::Parameter mF0{"Fundamental", 110.0f};   // base pitch Hz (+ V/oct)
    od::Parameter mLevel{"Level", 0.5f};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
