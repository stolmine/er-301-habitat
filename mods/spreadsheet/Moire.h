#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  // Moire - a modulated, coupled RESONANT network (moving intermod lattice, resonator core).
  //
  // The Trinity RE proved BLOCK's spectrum is an intermod lattice f(h,k)=fc*(h+k*r). Moire
  // exposes r as a playable, audio-rate control so the lattice MOVES - but the partials are
  // not sine oscillators, they are 2-pole RESONATORS tuned to the lattice frequencies, driven
  // by noise and by each other. That gives body (resonance), weight that accumulates in the
  // bank, and emergent behaviour from the coupling that a sine bank cannot. Not Rings: Rings'
  // modes are fixed and independent; here they MOVE (r/drift/lock) and DRIVE each other (couple).
  class Moire : public od::Object
  {
  public:
    Moire();
    virtual ~Moire();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mVOct{"V/Oct"};
    od::Inlet mSpread{"Spread"};   // r - the lattice detune; audio-rate, 0..2
    od::Inlet mBody{"Body"};       // resonator Q: breathy (0) -> sharp ring (1)
    od::Inlet mAir{"Air"};         // noise excitation level
    od::Inlet mCouple{"Couple"};   // resonator cross-feedback (the network/matrix seed)
    od::Inlet mDrift{"Drift"};     // per-resonator slow frequency wander (life)
    od::Inlet mLock{"Lock"};       // snap resonant frequencies toward the harmonic grid
    od::Outlet mOut{"Out"};

    od::Parameter mF0{"Fundamental", 110.0f};   // base pitch Hz (+ V/oct)
    od::Parameter mLevel{"Level", 0.5f};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
