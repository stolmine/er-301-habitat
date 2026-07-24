// house::Console0Channel
//
// Component atom (od::Object, instantiable from Lua; no Lua
// unit + no toc entry per feedback_atoms_as_components).
// Stereo encode side of the Console0 containment pair.
//
// SAT CURVE LIFTED VERBATIM from AW Console0Channel
// (Chris Johnson, MIT, ~/repos/airwindows/plugins/MacVST/
// Console0Channel/source/Console0ChannelProc.cpp lines 87-108).
// BigFastSin: clamped at ±1.4137 (≈√2), polynomial
//   y = (x/2) * (2.8274 - x)  for positive (mirror for negative)
// Bracketed by two 1-sample-delayed averaging LP filters
// (corner at fs/4).
//
// DELIBERATE DEVIATION from AW for habitat UX:
//   * Continuous gain via simple multiply, NOT bitshift+lookup.
//     AW maps gainControl to bitshift index then to a power-of-2
//     gain from a switch table -- discrete 6 dB steps, with A=0
//     mapping to gain=0.0 (mute) and A∈[0.6, 0.9] all collapsing
//     to gain=2.0 (no resolution). The sat CURVE math (the
//     defining Console0 character) is preserved verbatim; only
//     the gain CONTROL changes shape. Per feedback_no_third_party_branding,
//     keeping the name with a documented UX deviation is the
//     established precedent for derivative AW ports where the
//     sonic identity is in the math we preserved.
//   * Pan is simplified to linear attenuation rather than AW's
//     bitshift+switch -- same behavior at 0 / 0.5 / 1, smoother
//     between.
//
// Per-sample cost: ~15 ops + 2 LP averages per side. No
// transcendentals. Easily the cheapest sat atom in the house
// catalog -- direct consequence of the polynomial-only design.
//
// State: 4 doubles per instance (avgAL/AR/BL/BR for the two
// averaging filters). Trivial.
//
// LOAD-BEARING preservation:
//   * BigFastSin polynomial exactly: y = (x/2)*(2.8274-x)
//     with clamp at ±1.4137 -- these are the "magic constants"
//     that give the curve its compression shape
//   * Two LP averages bracketing the sat (not one, not three)
//   * Sat sign-mirrored: -(x/-2)*(2.8274+x) for negative
//     (preserves identity for x near zero)
//
// Dropped per template:
//   * Per-sample dither
//   * fpd RNG seeding (replaced by deterministic 1.18e-17 in
//     denormal flush)

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <string.h>

namespace house
{

  class Console0Channel : public od::Object
  {
  public:
    Console0Channel()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mGain);
      addParameter(mPan);

      avgAL = avgAR = 0.0;
      avgBL = avgBR = 0.0;
    }

    virtual ~Console0Channel() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mGain{"Gain", 0.5f};
    od::Parameter mPan{"Pan", 0.5f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      // ----- Block-rate gain bake (continuous, deviates from
      // AW's bitshift quantization for smooth UX) -----
      double gainKnob = (double)mGain.value();
      if (gainKnob < 0.0) gainKnob = 0.0;
      if (gainKnob > 1.0) gainKnob = 1.0;
      // Range [0.05, 1.95], with A=0.5 → gainScale=1.0 (unity).
      // Symmetric pre/post placement (Channel + Bus driven by
      // same Drive knob) at A=0.5 yields transparent round-trip.
      double gainScale = 0.05 + gainKnob * 1.9;

      // ----- Linear pan: 0.5 = center (both at full gain) -----
      double panKnob = (double)mPan.value();
      if (panKnob < 0.0) panKnob = 0.0;
      if (panKnob > 1.0) panKnob = 1.0;
      double panOffset = (panKnob - 0.5) * 2.0; // [-1, 1]
      double gainLd = gainScale;
      double gainRd = gainScale;
      if (panOffset > 0.0) gainLd *= (1.0 - panOffset);
      if (panOffset < 0.0) gainRd *= (1.0 + panOffset);
      // Bake block-rate gains to float once, so the per-sample path is pure float (the whole
      // hot loop is averaging LPs + a polynomial - float precision is ample, and this drops
      // the per-sample float<->double cast traps + slow scalar VFPv3 doubles on Cortex-A8).
      float gainL = (float)gainLd;
      float gainR = (float)gainRd;

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        float inputSampleL = *in1;
        float inputSampleR = *in2;
        if (fabsf(inputSampleL) < 1.18e-23f) inputSampleL = 1.18e-17f;
        if (fabsf(inputSampleR) < 1.18e-23f) inputSampleR = 1.18e-17f;

        // Pre-sat 1-sample-delayed averaging LP.
        float tempL = inputSampleL;
        inputSampleL = (inputSampleL + avgAL) * 0.5f;
        avgAL = tempL;
        float tempR = inputSampleR;
        inputSampleR = (inputSampleR + avgAR) * 0.5f;
        avgAR = tempR;

        // Apply gain.
        inputSampleL *= gainL;
        inputSampleR *= gainR;

        // BigFastSin sat curve (verbatim from AW source).
        if (inputSampleL > 1.4137166941154f) inputSampleL = 1.4137166941154f;
        if (inputSampleL < -1.4137166941154f) inputSampleL = -1.4137166941154f;
        if (inputSampleL > 0.0f)
          inputSampleL = (inputSampleL / 2.0f) * (2.8274333882308f - inputSampleL);
        else
          inputSampleL = -(inputSampleL / -2.0f) * (2.8274333882308f + inputSampleL);

        if (inputSampleR > 1.4137166941154f) inputSampleR = 1.4137166941154f;
        if (inputSampleR < -1.4137166941154f) inputSampleR = -1.4137166941154f;
        if (inputSampleR > 0.0f)
          inputSampleR = (inputSampleR / 2.0f) * (2.8274333882308f - inputSampleR);
        else
          inputSampleR = -(inputSampleR / -2.0f) * (2.8274333882308f + inputSampleR);

        // Post-sat averaging LP.
        tempL = inputSampleL;
        inputSampleL = (inputSampleL + avgBL) * 0.5f;
        avgBL = tempL;
        tempR = inputSampleR;
        inputSampleR = (inputSampleR + avgBR) * 0.5f;
        avgBR = tempR;

        *out1 = inputSampleL;
        *out2 = inputSampleR;
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    float avgAL, avgAR, avgBL, avgBR;
#endif
  };

} // namespace house
