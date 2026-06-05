// house::Console0Buss
//
// Component atom (od::Object, no Lua unit). Stereo decode side
// of the Console0 containment pair. Pairs with Console0Channel
// to wrap any inner chain in a level-dependent governor.
//
// DESAT CURVE LIFTED VERBATIM from AW Console0Buss
// (Chris Johnson, MIT, ~/repos/airwindows/plugins/MacVST/
// Console0Buss/source/Console0BussProc.cpp lines 95-108).
// BigFastArcSin: clamped at ±2.8, rational
//   y = (x * 2.0) / (3.0 - x)  for positive (mirror for negative)
// Bracketed by two 1-sample-delayed averaging LP filters.
//
// DELIBERATE DEVIATION from AW for habitat UX:
//   Continuous gain (same shape as Console0Channel here -- pair
//   them with matching Gain settings for symmetric round-trip,
//   which is the natural Console pair use). See Console0Channel.h
//   for the full deviation rationale.
//
// Per-sample cost: ~15 ops + 2 LP averages per side. No
// transcendentals.
//
// State: 4 doubles per instance.
//
// LOAD-BEARING preservation:
//   * BigFastArcSin rational exactly: y = (x*2)/(3-x) with
//     clamp at ±2.8 -- expands signals that Console0Channel
//     compressed
//   * Two LP averages bracketing the desat
//   * Sat sign-mirrored: -(x*-2)/(3+x) for negative
//
// NOTE: the round-trip Channel→inner_chain→Buss does NOT
// perfectly invert: desat(sat(x)) ≠ x exactly. The mismatch IS
// the Console "glue" character that AW intended.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <string.h>

namespace house
{

  class Console0Buss : public od::Object
  {
  public:
    Console0Buss()
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

    virtual ~Console0Buss() {}

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

      double gainKnob = (double)mGain.value();
      if (gainKnob < 0.0) gainKnob = 0.0;
      if (gainKnob > 1.0) gainKnob = 1.0;
      // Range [0.05, 1.95], matches Console0Channel so symmetric
      // Drive setting yields transparent round-trip at A=0.5.
      double gainScale = 0.05 + gainKnob * 1.9;

      double panKnob = (double)mPan.value();
      if (panKnob < 0.0) panKnob = 0.0;
      if (panKnob > 1.0) panKnob = 1.0;
      double panOffset = (panKnob - 0.5) * 2.0;
      double gainL = gainScale;
      double gainR = gainScale;
      if (panOffset > 0.0) gainL *= (1.0 - panOffset);
      if (panOffset < 0.0) gainR *= (1.0 + panOffset);

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        double inputSampleL = *in1;
        double inputSampleR = *in2;
        if (fabs(inputSampleL) < 1.18e-23) inputSampleL = 1.18e-17;
        if (fabs(inputSampleR) < 1.18e-23) inputSampleR = 1.18e-17;

        double tempL = inputSampleL;
        inputSampleL = (inputSampleL + avgAL) * 0.5;
        avgAL = tempL;
        double tempR = inputSampleR;
        inputSampleR = (inputSampleR + avgAR) * 0.5;
        avgAR = tempR;

        inputSampleL *= gainL;
        inputSampleR *= gainR;

        // BigFastArcSin desat curve (verbatim from AW source).
        if (inputSampleL > 2.8) inputSampleL = 2.8;
        if (inputSampleL < -2.8) inputSampleL = -2.8;
        if (inputSampleL > 0.0)
          inputSampleL = (inputSampleL * 2.0) / (3.0 - inputSampleL);
        else
          inputSampleL = -(inputSampleL * -2.0) / (3.0 + inputSampleL);

        if (inputSampleR > 2.8) inputSampleR = 2.8;
        if (inputSampleR < -2.8) inputSampleR = -2.8;
        if (inputSampleR > 0.0)
          inputSampleR = (inputSampleR * 2.0) / (3.0 - inputSampleR);
        else
          inputSampleR = -(inputSampleR * -2.0) / (3.0 + inputSampleR);

        tempL = inputSampleL;
        inputSampleL = (inputSampleL + avgBL) * 0.5;
        avgBL = tempL;
        tempR = inputSampleR;
        inputSampleR = (inputSampleR + avgBR) * 0.5;
        avgBR = tempR;

        *out1 = (float)inputSampleL;
        *out2 = (float)inputSampleR;
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    double avgAL, avgAR, avgBL, avgBR;
#endif
  };

} // namespace house
