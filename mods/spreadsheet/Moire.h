#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  // Moire - the moving intermod-lattice voice (v0, bare-bones oscillator/drone).
  //
  // The Trinity RE proved BLOCK's spectrum is an intermod lattice f(h,k)=fc*(h+k*r):
  // a core oscillator's harmonics h cross-modulated by a 2nd oscillator detuned by r.
  // The hardware sets r statically (Shape). Moire exposes r as a playable, audio-rate
  // control, so the whole lattice MOVES - partials slide through each other, sub-modes
  // (k<0, below fc) swell and vanish. Not a clone: no fitted table, fold, grit, or env.
  //
  // v0 = 15-partial odd-harmonic lattice (h in {1,3,5}, k in {-2..+2}), continuous.
  // See planning/moire-voice.md.
  class Moire : public od::Object
  {
  public:
    Moire();
    virtual ~Moire();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mVOct{"V/Oct"};
    od::Inlet mSpread{"Spread"};   // r - the star; audio-rate, 0..2
    od::Inlet mDrift{"Drift"};     // per-partial life: independent slow pitch drift, audio-rate
    od::Outlet mOut{"Out"};

    od::Parameter mF0{"Fundamental", 110.0f};   // base pitch Hz (+ V/oct)
    od::Parameter mLevel{"Level", 0.5f};
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace stolmine
