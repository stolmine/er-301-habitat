// house::ChainMix
//
// Component atom (od::Object, no Lua unit). Stereo dry/wet
// crossfade for chain-as-unit constructions where parallel
// dry/wet mix needs to happen at the end of a Lua-composed
// processing chain.
//
// Inlets: Dry L, Dry R, Wet L, Wet R
// Outlets: Out L, Out R
// Parameter: Mix (0..1; 0 = dry only, 1 = wet only, 0.5 = equal)
//
// Per-sample: out = dry * (1 - mix) + wet * mix
//
// Mix is block-rate (read once per process() call). For sample-
// rate Mix control via CV, the consuming unit can drive Mix via
// app.ParameterAdapter from a CV inlet -- standard habitat
// pattern.
//
// Per-sample cost: 4 muls + 2 add per side = ~10 cycles per
// side. State-free (other than the Parameter).
//
// Reusable for any future chain unit (Crush, Bloom, Smear, etc.)
// that wants parallel-processing wet/dry without baking a custom
// mixer into each unit's C++.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>

namespace house
{

  class ChainMix : public od::Object
  {
  public:
    ChainMix()
    {
      addInput(mDryL);
      addInput(mDryR);
      addInput(mWetL);
      addInput(mWetR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mMix);
    }

    virtual ~ChainMix() {}

#ifndef SWIGLUA
    od::Inlet     mDryL{"Dry L"};
    od::Inlet     mDryR{"Dry R"};
    od::Inlet     mWetL{"Wet L"};
    od::Inlet     mWetR{"Wet R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mMix{"Mix", 1.0f};

    virtual void process()
    {
      float *dryL = mDryL.buffer();
      float *dryR = mDryR.buffer();
      float *wetL = mWetL.buffer();
      float *wetR = mWetR.buffer();
      float *outL = mOutL.buffer();
      float *outR = mOutR.buffer();

      float mix = mMix.value();
      if (mix < 0.0f) mix = 0.0f;
      if (mix > 1.0f) mix = 1.0f;
      float oneMinusMix = 1.0f - mix;

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        *outL++ = (*dryL++) * oneMinusMix + (*wetL++) * mix;
        *outR++ = (*dryR++) * oneMinusMix + (*wetR++) * mix;
      }
    }
#endif
  };

} // namespace house
