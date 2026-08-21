// house::Gesso - bus compressor.
//
// The unit wrapper over GlueComp.h. Engine detail, the research behind
// the Character control, and the acceptance tests are all in
// planning/compressor-character-research.md and the atom header.
//
// CHARACTER IS THE COLOUR CONTROL, replacing the Sag knob the original
// design note proposed. Sag was a continuous 0..1 amount, which is the
// same shape as Parametric EQ's first Drive control - subtle
// everywhere, nothing to A/B against, and judged inert in use. Each
// Character position instead changes FOUR sidechain laws at once:
// detector placement, detector type, timing law and knee.
#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include "GlueComp.h"

namespace house
{

  class Gesso : public od::Object
  {
  public:
    Gesso()
    {
      addInput(mInL); addInput(mInR);
      addOutput(mOutL); addOutput(mOutR);
      addParameter(mThreshold);
      addParameter(mRatio);
      addParameter(mAttack);
      addParameter(mRelease);
      addParameter(mMakeup);
      addParameter(mMix);
      addOption(mCharacter);
      mCharacter.enableSerialization();
    }
    virtual ~Gesso() {}

#ifndef SWIGLUA
    od::Inlet mInL{"In L"};
    od::Inlet mInR{"In R"};
    od::Outlet mOutL{"Out L"};
    od::Outlet mOutR{"Out R"};

    od::Parameter mThreshold{"Threshold", 0.5f};
    od::Parameter mRatio{"Ratio", 0.0f};
    od::Parameter mAttack{"Attack", 0.01f};
    od::Parameter mRelease{"Release", 0.2f};
    // MAKEUP IS ITS OWN CONTROL, not borrowed from a master level. The
    // Channel Strip review found that having no makeup on the
    // compressor meant the only recovery was a shared output level,
    // which is the wrong control for the job.
    od::Parameter mMakeup{"Makeup", 1.0f};
    od::Parameter mMix{"Mix", 1.0f};

    // 1 Glue, 2 Peak, 3 Opto. Three, because OptionControl has exactly
    // three sub-buttons and a fourth choice is unreachable.
    od::Option mCharacter{"Char", 1};

    virtual void process()
    {
      float *inL = mInL.buffer(), *inR = mInR.buffer();
      float *outL = mOutL.buffer(), *outR = mOutR.buffer();
      const float sr = globalConfig.sampleRate > 0.0f ? globalConfig.sampleRate : 48000.0f;

      int ch = mCharacter.value();
      if (ch < 1) ch = 1; else if (ch > 3) ch = 3;
      glueCompBake(mC, (GlueCompCharacter)(ch - 1),
                   CLAMP(0.001f, 1.0f, mThreshold.value()),
                   CLAMP(0.0f, 1.0f, mRatio.value()),
                   CLAMP(0.00002f, 0.1f, mAttack.value()),
                   CLAMP(0.002f, 2.0f, mRelease.value()),
                   CLAMP(0.0f, 8.0f, mMakeup.value()), sr);

      for (int i = 0; i < FRAMELENGTH; i++) { mL[i] = inL[i]; mR[i] = inR[i]; }
      // Ratio 0 is an exact bypass, so a parked compressor costs nothing
      // and colours nothing.
      if (mC.ratioAmt > 0.0f || mC.makeup != 1.0f)
        mComp.processBlock(mL, mR, FRAMELENGTH, mC);

      // LINEAR crossfade: a compressor's output is the same signal with
      // its dynamics altered, so it is highly correlated with the dry
      // and an equal-power law would bulge at centre. Parallel
      // compression is the whole point of this control.
      const float mix = CLAMP(0.0f, 1.0f, mMix.value());
      const float dry = 1.0f - mix;
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        outL[i] = inL[i] * dry + mL[i] * mix;
        outR[i] = inR[i] * dry + mR[i] * mix;
      }
    }
#endif

  private:
    GlueCompCoefs mC;
    GlueCompStereo mComp;
    float mL[512];
    float mR[512];
  };

} // namespace house
