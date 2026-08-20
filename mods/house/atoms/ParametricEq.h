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
      addParameter(mMix);
      addOption(mCharacter);
      addOption(mLfShape);
      addOption(mHfShape);
      // od::Option is NOT auto-serialized.
      mCharacter.enableSerialization();
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
    od::Parameter mMix{"Mix", 1.0f};

    // CHARACTER, one discrete control replacing the old Colour option
    // and the separate Drive fader.
    //
    // Drive was a continuous ply whose whole range was subtle (0.3% to
    // 8% THD) and which did nothing at all on a flat EQ, so there was
    // never anything to A/B against and users could not tell what it
    // was doing. Colour meanwhile was a second character control buried
    // in the menu. Two overlapping character controls, one invisible
    // and one illegible.
    //
    // Now one control on a PLY, moving Q law, gain range and saturation
    // together - which is what the design note said Colour should do
    // all along. Discrete positions can be compared against each other,
    // which a subtle continuous knob cannot.
    //
    // od::Option values are 1-based; 0 means UNKNOWN.
    // DEFAULTS TO CLEAN. An EQ should be accurate out of the box and
    // character should be opt-in: saturating the band tap necessarily
    // pulls the peak down, so a +12 dB request measures +10.8 dB at
    // brown. Clean keeps the band landing exactly on its number, and
    // keeps clean-plus-flat-gains a bit-identical bypass.
    //   1 Clean  constant-Q, +/-15 dB, no saturation
    //   2 Brown  constant-Q, +/-15 dB, light
    //   3 Black  proportional-Q, +/-18 dB, medium
    //   4 Hot    proportional-Q, +/-18 dB, heavy
    od::Option mCharacter{"Character", 1};
    od::Option mLfShape{"LF Shape", 1};   // 1 shelf, 2 bell
    od::Option mHfShape{"HF Shape", 1};   // 1 shelf, 2 bell

    virtual void process()
    {
      float *inL = mInL.buffer();
      float *inR = mInR.buffer();
      float *outL = mOutL.buffer();
      float *outR = mOutR.buffer();

      const float sr = globalConfig.sampleRate > 0.0f ? globalConfig.sampleRate : 48000.0f;
      int ch = mCharacter.value();
      if (ch < 1) ch = 1; else if (ch > 4) ch = 4;
      const bool hotHalf = (ch >= 3);
      const ParametricBandQLaw law = hotHalf ? PARAM_BAND_Q_PROPORTIONAL
                                             : PARAM_BAND_Q_CONSTANT;
      // Range moves with the Q law, as the two documented circuit
      // revisions do: the extra 3 dB changes how hard people push it.
      const float maxDb = hotHalf ? 18.0f : 15.0f;
      // Saturation per position. Clean is exactly 0, so Clean plus flat
      // gains is still a bit-identical bypass.
      static const float kDrive[4] = { 0.0f, 0.35f, 0.7f, 1.0f };
      const float drive = kDrive[ch - 1];

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
      // SKIP INACTIVE BANDS. A band at 0 dB has gain exactly 0 and
      // contributes in + 0*tap, i.e. nothing, so running it is pure
      // waste. Most EQ patches use one or two bands, not four.
      // Measured 6% stereo CPU on hardware with all four live; this
      // takes typical use to roughly 1.5-3%.
      //
      // The test is at BLOCK rate on a baked coefficient, not per
      // sample on a parameter, so it is not a sample-loop branch
      // (feedback_runtime_branched_dsp_dispatch).
      //
      // A skipped band's state is deliberately NOT reset: leaving it
      // frozen means re-enabling the band resumes from where it was
      // rather than from a discontinuity. Its output was zero either
      // way, so no energy is trapped.
      for (int b = 0; b < 4; b++)
        if (mC[b].gain != 0.0f)
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
