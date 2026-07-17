#pragma once

// Vivary -- a generative noise/texture source built on a 1D ELEMENTARY cellular
// automaton. The CA row IS the wavetable; a read head scans it at Freq, and the
// CA advances one generation at each wavetable-pass BOUNDARY (phase-continuous,
// click-free) every NClk passes -- so NClk high = a static structured tone,
// NClk=1 = a new generation every cycle = aperiodic rule-structured NOISE.
//   Family (3)  = chaos (radius-1) / structure (radius-2) / gliders (edge-of-chaos).
//   Rule        = index into the curated rule table = the character.
//   Res         = active cell count at constant pitch (brute square -> detail).
//   Evolve      = static tone .. per-pass noise.
//   Reset       = reseed interval to keep converging rules alive (0 = off; a
//                 die-out watchdog also reseeds so it never goes silent).
//   Overlap     = grain layering of recent generations (decorrelated).
//   Feedback    = pitch-tracked comb resonance.
//
// The DSP lives in CellularEngine (shared with Rauschen's CA algorithm). This
// unit is the thin full-control wrapper: every parameter on its own knob/CV.
// Inspired-in-spirit by Kentaro's tonemata; the CA is public-domain math
// (clean-room), generic name ([[feedback_no_third_party_branding]]).
//
// am335x: all virtuals inline (feedback_no_out_of_line_virtuals). No Vivary.cpp.

#include <od/objects/Object.h>
#include "CellularEngine.h"

namespace stolmine
{
  class Vivary : public od::Object
  {
  public:
    Vivary()
    {
      addInput(mVOct);
      addOutput(mOut);
      addParameter(mFreq);
      addParameter(mRule);
      addParameter(mRes);
      addParameter(mEvolve);
      addParameter(mReset);
      addParameter(mFamily);
      addParameter(mOverlap);
      addParameter(mFeedback);
      mEngine.init();
    }

    virtual ~Vivary() {}

#ifndef SWIGLUA
    virtual void process()
    {
      float *out = mOut.buffer();

      // Freq is a normal oscillator f0 (Hz, oscFreq map in Lua) with a V/Oct
      // input: f0 = fundamental * 2^(V/Oct*10) (ER-301 pitch convention). The
      // other controls stay normalized 0..1 for easy modulation.
      const float voct = mVOct.buffer()[0];
      float fund = mFreq.value();
      if (!(fund >= 0.0f)) fund = 0.0f;
      float f0 = fund * powf(2.0f, voct * 10.0f);
      if (f0 < 0.0f) f0 = 0.0f;
      const float f0Max = globalConfig.sampleRate * 0.49f;
      if (f0 > f0Max) f0 = f0Max;

      float familyN = mFamily.value();
      if (!(familyN >= 0.0f)) familyN = 0.0f; else if (familyN > 1.0f) familyN = 1.0f;
      int family = (int)(familyN * 2.0f + 0.5f);

      mEngine.setup(family, mRule.value(), mRes.value(), mEvolve.value(),
                    mReset.value(), mOverlap.value(), mFeedback.value(),
                    f0, globalConfig.sampleRate);

      for (int n = 0; n < FRAMELENGTH; n++)
        out[n] = mEngine.tick();
    }

    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};
    od::Parameter mFreq{"Freq", 110.0f};       // Hz fundamental (oscFreq map in Lua)
    od::Parameter mRule{"Rule", 0.0f};         // 0..1 -> curated rule index
    od::Parameter mRes{"Res", 0.24f};          // 0..1 -> 2..256 cells
    od::Parameter mEvolve{"Evolve", 1.0f};     // 0..1 -> static tone .. per-pass noise
    od::Parameter mReset{"Reset", 0.0f};       // 0..1 -> reseed interval (0 = off)
    od::Parameter mFamily{"Family", 0.0f};     // 0..1 -> chaos / structure / glider
    od::Parameter mOverlap{"Overlap", 0.0f};   // 0..1 -> grain overlap (gen layering)
    od::Parameter mFeedback{"Feedback", 0.0f}; // 0..1 -> pitch-tracked comb resonance

  private:
    CellularEngine mEngine;
#endif
  };

} // namespace stolmine
