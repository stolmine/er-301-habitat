// house::ParametricEq
//
// Four-band parametric EQ built from four ParametricBandStereo, one
// per band, each handling L and R together. This is the standalone consumer of the band atom; the other
// is strata-channel-strip's EQ section, which instantiates the same
// atom directly.
//
// WHY THIS EXISTS NEXT TO THE FIRMWARE'S EQ3. Measured, not assumed:
// asked for +12 dB at its mid band, EQ3 delivers +9.9 dB at 250 Hz and
// +9.1 dB at 4 kHz - its "mid" is over six octaves wide, and it
// overshoots to +13.1 dB. It is a three-way crossover splitter (four
// one-pole filters give a lowpass, four more a highpass reference, the
// mid is the remainder), so it has NO Q AT ALL and its two frequencies
// are crossover points rather than band centres. This unit at Q=4
// lands 12.00 dB on the nose and is within 0.4 dB of flat one octave
// away. They are different tools: EQ3 is a broad tone control, this is
// corrective.
//
// EQ3 keeps one genuine advantage that this unit deliberately does NOT
// chase: its three gains are Inlets, so they take audio-rate CV. Here
// every control is block-rate through a ParameterAdapter, because
// twelve inlets to duplicate a capability the firmware already ships
// would be poor value. Reach for EQ3 when the gain itself is the
// modulation target.
//
// BAND LAYOUT, from the SSL 611 published spec via
// planning/ochre-character-eq.md. This is a behavioural design
// informed by published specification, NOT a circuit emulation, and
// nothing user-facing claims otherwise.
//
//   LF   30 - 450 Hz    shelf, switchable to bell
//   LMF  200 - 2500 Hz  bell, with Q
//   HMF  600 - 7000 Hz  bell, with Q
//   HF   1.5k - 16 kHz  shelf, switchable to bell
//
// COLOUR switches the Q law and the gain range together, which is the
// one behavioural difference between the two documented circuit
// revisions that can be reproduced honestly:
//   Brown - constant-octave Q, +/-15 dB
//   Black - proportional Q (tightens as gain rises), +/-18 dB
//
// ALL GAINS AT ZERO IS AN EXACT BYPASS, inherited from the band atom:
// each band returns `in + gain*tap` and gain is (10^(dB/20) - 1), so
// 0 dB contributes literally nothing. Four bands in series of exact
// bypasses is still an exact bypass. Measured in
// tools/parametric-eq-test rather than assumed.
#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include "ParametricBand.h"

namespace house
{

  class ParametricEq : public od::Object
  {
  public:
    ParametricEq()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mLfFreq);
      addParameter(mLfGain);
      addParameter(mLmfFreq);
      addParameter(mLmfGain);
      addParameter(mLmfQ);
      addParameter(mHmfFreq);
      addParameter(mHmfGain);
      addParameter(mHmfQ);
      addParameter(mHfFreq);
      addParameter(mHfGain);
      addParameter(mDrive);
      addParameter(mMix);
      addOption(mColour);
      addOption(mLfShape);
      addOption(mHfShape);
      // od::Option is NOT auto-serialized.
      mColour.enableSerialization();
      mLfShape.enableSerialization();
      mHfShape.enableSerialization();
    }

    virtual ~ParametricEq() {}

#ifndef SWIGLUA
    od::Inlet mInL{"In L"};
    od::Inlet mInR{"In R"};
    od::Outlet mOutL{"Out L"};
    od::Outlet mOutR{"Out R"};

    od::Parameter mLfFreq{"LF Freq", 100.0f};
    od::Parameter mLfGain{"LF Gain", 0.0f};
    od::Parameter mLmfFreq{"LMF Freq", 500.0f};
    od::Parameter mLmfGain{"LMF Gain", 0.0f};
    od::Parameter mLmfQ{"LMF Q", 1.0f};
    od::Parameter mHmfFreq{"HMF Freq", 2000.0f};
    od::Parameter mHmfGain{"HMF Gain", 0.0f};
    od::Parameter mHmfQ{"HMF Q", 1.0f};
    od::Parameter mHfFreq{"HF Freq", 8000.0f};
    od::Parameter mHfGain{"HF Gain", 0.0f};
    od::Parameter mDrive{"Drive", 0.0f};
    od::Parameter mMix{"Mix", 1.0f};

    // 1 = Brown (constant Q, +/-15 dB), 2 = Black (proportional Q,
    // +/-18 dB). od::Option values are 1-based; 0 means UNKNOWN.
    od::Option mColour{"Colour", 1};
    od::Option mLfShape{"LF Shape", 1};   // 1 shelf, 2 bell
    od::Option mHfShape{"HF Shape", 1};   // 1 shelf, 2 bell

    virtual void process()
    {
      float *inL = mInL.buffer();
      float *inR = mInR.buffer();
      float *outL = mOutL.buffer();
      float *outR = mOutR.buffer();

      const float sr = globalConfig.sampleRate > 0.0f ? globalConfig.sampleRate : 48000.0f;
      const bool black = (mColour.value() == 2);
      const ParametricBandQLaw law = black ? PARAM_BAND_Q_PROPORTIONAL
                                           : PARAM_BAND_Q_CONSTANT;
      // The two circuit revisions differ in range as well as Q law,
      // and that difference is behavioural rather than cosmetic: the
      // extra 3 dB changes how hard people push it.
      const float maxDb = black ? 18.0f : 15.0f;
      const float drive = CLAMP(0.0f, 1.0f, mDrive.value());

      // Coefficients are baked ONCE PER BLOCK, never per sample: the
      // bake contains tan(), pow() and a divide.
      // feedback_runtime_branched_dsp_dispatch - the Colour and shape
      // options are coefficient decisions here, not sample-loop
      // branches.
      const ParametricBandShape lfShape =
          (mLfShape.value() == 2) ? PARAM_BAND_BELL : PARAM_BAND_LOW_SHELF;
      const ParametricBandShape hfShape =
          (mHfShape.value() == 2) ? PARAM_BAND_BELL : PARAM_BAND_HIGH_SHELF;

      parametricBandBake(mC[0], CLAMP(30.0f, 450.0f, mLfFreq.value()),
                         CLAMP(-maxDb, maxDb, mLfGain.value()), 0.7,
                         drive, sr, lfShape, law);
      parametricBandBake(mC[1], CLAMP(200.0f, 2500.0f, mLmfFreq.value()),
                         CLAMP(-maxDb, maxDb, mLmfGain.value()),
                         CLAMP(0.3f, 10.0f, mLmfQ.value()),
                         drive, sr, PARAM_BAND_BELL, law);
      parametricBandBake(mC[2], CLAMP(600.0f, 7000.0f, mHmfFreq.value()),
                         CLAMP(-maxDb, maxDb, mHmfGain.value()),
                         CLAMP(0.3f, 10.0f, mHmfQ.value()),
                         drive, sr, PARAM_BAND_BELL, law);
      parametricBandBake(mC[3], CLAMP(1500.0f, 16000.0f, mHfFreq.value()),
                         CLAMP(-maxDb, maxDb, mHfGain.value()), 0.7,
                         drive, sr, hfShape, law);

      const float mix = CLAMP(0.0f, 1.0f, mMix.value());
      // LINEAR crossfade, deliberately, against the equal-power rule
      // (feedback_equal_power_drywet_crossfade). That rule exists for
      // DECORRELATED wet signals; an EQ's output is the same signal
      // with a tilt, so it is highly correlated with the dry and an
      // equal-power law would bulge by up to +3 dB at centre. The
      // ochre note specifies linear for exactly this reason.
      const float dry = 1.0f - mix;

      // BAND-MAJOR, NEON-STEREO. Three measured reasons, all on real
      // Cortex-A8 codegen:
      //   - sample-major cost 41.5 instructions per band per channel,
      //     because four bands hold 32 coefficients, which exceeds the
      //     32 VFP s-registers and spills every sample. Band-major
      //     keeps one band's coefficients resident: 29.
      //   - L and R are independent and elementwise, so the natural
      //     2-wide NEON axis halves that again: 30 instructions per
      //     band for BOTH channels. Same axis EQ3 uses.
      //   - net 332 -> 120 instructions per sample for stereo 4-band.
      // Scratch is a CLASS MEMBER, never a stack local
      // (feedback_neon_intrinsics_drumvoice).
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        mWetL[i] = inL[i];
        mWetR[i] = inR[i];
      }
      for (int b = 0; b < 4; b++)
        mBand[b].processBlock(mWetL, mWetR, FRAMELENGTH, mC[b]);
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        outL[i] = inL[i] * dry + mWetL[i] * mix;
        outR[i] = inR[i] * dry + mWetR[i] * mix;
      }
    }
#endif

  private:
    ParametricBandCoefs mC[4];
    ParametricBandStereo mBand[4];
    // Class members, not stack locals: a NEON path with stack-local
    // arrays emits trapping vld1.32 [reg :64] on A8.
    float mWetL[512];
    float mWetR[512];
  };

} // namespace house
