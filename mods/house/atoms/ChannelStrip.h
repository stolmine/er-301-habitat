// house::ChannelStrip
//
// Six sections in signal-flow order, each with a TRUE bypass. Phase 2
// of planning/strata-channel-strip.md: the skeleton plus the sections
// whose atoms already exist and are measured.
//
//   1 DYNAMICS  Pop3Dynamics       gate + compressor, one shared detector
//   2 FILTER    ParametricBand x2  HP + LP, replacing mode
//   3 EQ        ParametricBand x3  low / mid / high
//   4 DRIVE     DriveStage         Channel9 saturation + slew clip
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
// Drive and Punch are still stubs, deliberately: wiring the skeleton
// first means the null test is proven now, and each stub is then
// replaced in isolation against a test that already passes.
//
// FILTER IS NOT Capacitor2, which the design note named. Measured on
// real A8 codegen, Capacitor2Mono is 356 instructions with 223
// DOUBLE-precision ops, and that is MONO - roughly 712 stereo, against
// 30 for a ParametricBand stereo pass. Doubles fall to scalar VFP on
// A8 and its per-sample coefficient modulation makes nothing
// hoistable. A utility highpass does not need that; character belongs
// in the Drive section.
#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include "ParametricBand.h"
#include "Pop3Dynamics.h"
#include "DriveStage.h"

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

      addParameter(mHpFreq);
      addParameter(mLpFreq);

      addParameter(mEqLowGain);
      addParameter(mEqMidFreq);
      addParameter(mEqMidGain);
      addParameter(mEqMidQ);
      addParameter(mEqHighGain);

      addParameter(mDrive);
      addParameter(mSlew);

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

    // Filter. Headline is the highpass, which is what gets reached for.
    od::Parameter mHpFreq{"HP Freq", 10.0f};
    od::Parameter mLpFreq{"LP Freq", 20000.0f};

    // EQ. Three bands off the shared atom; the strip does not need the
    // standalone unit's four.
    od::Parameter mEqLowGain{"EQ Low", 0.0f};
    od::Parameter mEqMidFreq{"EQ Mid Freq", 1000.0f};
    od::Parameter mEqMidGain{"EQ Mid", 0.0f};
    od::Parameter mEqMidQ{"EQ Mid Q", 1.0f};
    od::Parameter mEqHighGain{"EQ High", 0.0f};

    // Drive. One knob spanning dry -> Spiral -> Density, plus the
    // acceleration-limiting slew clipper.
    od::Parameter mDrive{"Drive", 0.0f};
    od::Parameter mSlew{"Slew", 0.0f};

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

      // ---- 2. FILTER ----
      // Two SVF bands in REPLACING mode, so the same 30-instruction
      // stereo NEON kernel that serves the EQ serves this too. Each
      // side skips itself when parked at its extreme, so a strip using
      // only the highpass does not pay for a lowpass at 20 kHz.
      if (mFilterEngage.value() == 1)
      {
        // The DEFAULTS ARE THE PARKED POSITIONS, and the skip tests
        // against the clamp bounds rather than an arbitrary threshold.
        // Otherwise an engaged-but-untouched Filter still runs a real
        // filter: the default used to be 20 Hz against a skip at 11 Hz,
        // so it coloured the signal without the user asking. The
        // harness caught it.
        const float kHpMin = 10.0f, kLpMax = 20000.0f;
        const float hp = CLAMP(kHpMin, 2000.0f, mHpFreq.value());
        const float lp = CLAMP(1000.0f, kLpMax, mLpFreq.value());
        if (hp > kHpMin)
        {
          parametricBandBakeFilter(mFltC[0], hp, 0.707, sr, true);
          mFltBand[0].processBlock(mL, mR, FRAMELENGTH, mFltC[0]);
        }
        if (lp < kLpMax)
        {
          parametricBandBakeFilter(mFltC[1], lp, 0.707, sr, false);
          mFltBand[1].processBlock(mL, mR, FRAMELENGTH, mFltC[1]);
        }
      }


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

      // ---- 4. DRIVE ----
      if (mDriveEngage.value() == 1)
      {
        driveBake(mDrvC, CLAMP(0.0f, 1.0f, mDrive.value()),
                  CLAMP(0.0f, 1.0f, mSlew.value()));
        // Skips itself when both controls are at zero, so an engaged
        // but untouched Drive costs nothing and colours nothing.
        if (mDrvC.engaged != 0.0f)
          mDrv.processBlock(mL, mR, FRAMELENGTH, mDrvC);
      }

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

    DriveCoefs mDrvC;
    DriveStageStereo mDrv;

    ParametricBandCoefs mFltC[2];
    ParametricBandStereo mFltBand[2];

    ParametricBandCoefs mEqC[3];
    ParametricBandStereo mEqBand[3];
  };

} // namespace house
