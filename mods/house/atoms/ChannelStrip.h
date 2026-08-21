// house::ChannelStrip
//
// Six sections in signal-flow order, each with a TRUE bypass. Phase 2
// of planning/strata-channel-strip.md: the skeleton plus the sections
// whose atoms already exist and are measured.
//
//   1 DYNAMICS  Pop3Dynamics       gate + compressor, one shared detector
//   2 FILTER    (stub)             HP + LP, Capacitor2
//   3 EQ        ParametricBand x3  low / mid / high
//   4 DRIVE     (stub)             Channel9
//   5 PUNCH     (stub)             Point + Distance2
//   6 OUT       level + soft clip
//
// SIX, NOT SEVEN. Gate and Comp are one section because they are one
// atom: Pop3Dynamics shares a single detector path and its gate
// deliberately reads the UNCOMPRESSED signal from inside that shared
// structure. Splitting them would misrepresent the implementation, and
// a true-bypass saving cannot be taken for one without the other.
// Independent disable survives via each half's own ratio at 0.
//
// TRUE BYPASS, NOT UNITY GAIN. A bypassed section is SKIPPED, so it
// costs nothing - which is the entire point, since a strip with two
// sections in use should not cost what six do. The engage flags are
// resolved at BLOCK rate and the skip is a block-level branch; nothing
// switches per sample inside a section
// (feedback_runtime_branched_dsp_dispatch).
//
// The stub sections are deliberately real bypasses rather than absent:
// wiring the skeleton first means the null test below can be proven
// now, and each stub is then replaced in isolation against a test that
// already passes.
#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include "ParametricBand.h"
#include "Pop3Dynamics.h"

namespace house
{

  class ChannelStrip : public od::Object
  {
  public:
    ChannelStrip()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);

      addParameter(mDynAmount);
      addParameter(mDynThresh);
      addParameter(mDynAttack);
      addParameter(mDynRelease);
      addParameter(mGateThresh);
      addParameter(mGateAmount);

      addParameter(mEqLowGain);
      addParameter(mEqMidFreq);
      addParameter(mEqMidGain);
      addParameter(mEqMidQ);
      addParameter(mEqHighGain);

      addParameter(mOutLevel);

      addOption(mDynEngage);
      addOption(mFilterEngage);
      addOption(mEqEngage);
      addOption(mDriveEngage);
      addOption(mPunchEngage);
      addOption(mOutEngage);
      mDynEngage.enableSerialization();
      mFilterEngage.enableSerialization();
      mEqEngage.enableSerialization();
      mDriveEngage.enableSerialization();
      mPunchEngage.enableSerialization();
      mOutEngage.enableSerialization();
    }

    virtual ~ChannelStrip() {}

#ifndef SWIGLUA
    od::Inlet mInL{"In L"};
    od::Inlet mInR{"In R"};
    od::Outlet mOutL{"Out L"};
    od::Outlet mOutR{"Out R"};

    // Dynamics. Headline is Compress; the rest live in its expansion.
    od::Parameter mDynAmount{"Compress", 0.0f};
    od::Parameter mDynThresh{"Comp Thresh", 0.5f};
    od::Parameter mDynAttack{"Comp Attack", 0.3f};
    od::Parameter mDynRelease{"Comp Release", 0.5f};
    od::Parameter mGateThresh{"Gate Thresh", 0.0f};
    od::Parameter mGateAmount{"Gate Amount", 0.0f};

    // EQ. Three bands off the shared atom; the strip does not need the
    // standalone unit's four.
    od::Parameter mEqLowGain{"EQ Low", 0.0f};
    od::Parameter mEqMidFreq{"EQ Mid Freq", 1000.0f};
    od::Parameter mEqMidGain{"EQ Mid", 0.0f};
    od::Parameter mEqMidQ{"EQ Mid Q", 1.0f};
    od::Parameter mEqHighGain{"EQ High", 0.0f};

    od::Parameter mOutLevel{"Level", 1.0f};

    // 1 = engaged, 2 = bypassed. od::Option is 1-based; 0 is UNKNOWN.
    od::Option mDynEngage{"Dyn On", 2};
    od::Option mFilterEngage{"Flt On", 2};
    od::Option mEqEngage{"EQ On", 2};
    od::Option mDriveEngage{"Drv On", 2};
    od::Option mPunchEngage{"Pch On", 2};
    od::Option mOutEngage{"Out On", 2};

    virtual void process()
    {
      float *inL = mInL.buffer();
      float *inR = mInR.buffer();
      float *outL = mOutL.buffer();
      float *outR = mOutR.buffer();
      const float sr = globalConfig.sampleRate > 0.0f ? globalConfig.sampleRate : 48000.0f;

      // Everything runs in the scratch pair, so a fully bypassed strip
      // is a copy and nothing else. Class members, never stack locals
      // (feedback_neon_intrinsics_drumvoice).
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        mL[i] = inL[i];
        mR[i] = inR[i];
      }

      // ---- 1. DYNAMICS ----
      if (mDynEngage.value() == 1)
      {
        pop3Bake(mDynC,
                 CLAMP(0.0f, 1.0f, mDynThresh.value()),
                 CLAMP(0.0f, 1.0f, mDynAmount.value()),
                 CLAMP(0.0f, 1.0f, mDynAttack.value()),
                 CLAMP(0.0f, 1.0f, mDynRelease.value()),
                 CLAMP(0.0f, 1.0f, mGateThresh.value()),
                 CLAMP(0.0f, 1.0f, mGateAmount.value()),
                 0.5, 0.5, sr);
        for (int i = 0; i < FRAMELENGTH; i++)
        {
          mDynL.observe(mL[i], mDynC);
          mDynR.observe(mR[i], mDynC);
          pop3StereoLink(mDynL, mDynR, mDynC);
          mL[i] = mDynL.apply(mL[i], mDynC);
          mR[i] = mDynR.apply(mR[i], mDynC);
        }
      }

      // ---- 2. FILTER (stub) ----

      // ---- 3. EQ ----
      if (mEqEngage.value() == 1)
      {
        parametricBandBake(mEqC[0], 120.0, CLAMP(-15.0f, 15.0f, mEqLowGain.value()),
                           0.7, 0.0, sr, PARAM_BAND_LOW_SHELF, PARAM_BAND_Q_CONSTANT);
        parametricBandBake(mEqC[1], CLAMP(120.0f, 8000.0f, mEqMidFreq.value()),
                           CLAMP(-15.0f, 15.0f, mEqMidGain.value()),
                           CLAMP(0.3f, 10.0f, mEqMidQ.value()),
                           0.0, sr, PARAM_BAND_BELL, PARAM_BAND_Q_CONSTANT);
        parametricBandBake(mEqC[2], 8000.0, CLAMP(-15.0f, 15.0f, mEqHighGain.value()),
                           0.7, 0.0, sr, PARAM_BAND_HIGH_SHELF, PARAM_BAND_Q_CONSTANT);
        for (int b = 0; b < 3; b++)
          if (mEqC[b].gain != 0.0f)
            mEqBand[b].processBlock(mL, mR, FRAMELENGTH, mEqC[b]);
      }

      // ---- 4. DRIVE (stub) ----
      // ---- 5. PUNCH (stub) ----

      // ---- 6. OUT ----
      if (mOutEngage.value() == 1)
      {
        const float lv = CLAMP(0.0f, 4.0f, mOutLevel.value());
        for (int i = 0; i < FRAMELENGTH; i++)
        {
          // ClipOnly-style: linear until it is not, then hard-limited.
          // No transcendental, and exactly transparent below the knee.
          float a = mL[i] * lv, b = mR[i] * lv;
          if (a > 1.0f) a = 1.0f; else if (a < -1.0f) a = -1.0f;
          if (b > 1.0f) b = 1.0f; else if (b < -1.0f) b = -1.0f;
          mL[i] = a; mR[i] = b;
        }
      }

      for (int i = 0; i < FRAMELENGTH; i++)
      {
        outL[i] = mL[i];
        outR[i] = mR[i];
      }
    }
#endif

  private:
    float mL[512];
    float mR[512];

    Pop3Coefs mDynC;
    Pop3Mono mDynL, mDynR;

    ParametricBandCoefs mEqC[3];
    ParametricBandStereo mEqBand[3];
  };

} // namespace house
