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
    // Each position changes the CURVE LAW and the TOPOLOGY, not a
    // saturation amount. That ordering is the whole point: the previous
    // ladder moved saturation at every step and the Q law exactly once,
    // so only the one step that changed the law read as different. A
    // Q-law change is a far bigger perceptual event than a percent of
    // THD. Research and measurements: planning/eq-character-research.md.
    //
    //   1 CONSOLE  constant Q, series, +/-15 dB, light saturation.
    //              Transparent and surgical. The accurate position: the
    //              peak lands on the number asked for.
    //   2 PUNCH    SKIRT-PINNED proportional Q, series, +/-12 dB.
    //              Tightens as it is pushed while the skirt stays put,
    //              which is what lets this kind of curve tolerate
    //              aggressive settings.
    //   3 PASSIVE  broad shelves, PARALLEL topology, asymmetric
    //              +13.5/-17.5 dB. Boost and cut are not mirror images,
    //              and bands sum against dry rather than chaining.
    //
    // THE TOPOLOGY DIFFERENCE IS THE LARGEST SINGLE AXIS and it is free.
    // Measured: four bands all at 1 kHz and +6 dB give +24.00 dB in
    // series against +13.95 dB in parallel. Ten decibels from routing
    // alone. Sources call additive-vs-non-additive "one of the most
    // underrated factors" in why EQs sound different; series is the most
    // "digital" choice available and passive designs do not behave that
    // way.
    //
    // THREE POSITIONS, NOT FOUR. Unit.ViewControl.OptionControl draws
    // Drawings.Sub.ThreeColumns and maps sub-button i straight to choice
    // i, and there are three sub-buttons - a fourth is placed off-screen
    // and CANNOT BE SELECTED. An earlier four-position set shipped with
    // its last position unreachable on hardware.
    //
    // Exact bypass is a property of GAIN, not of character: a band
    // contributes gain*saturate(tap) and gain is exactly 0 at 0 dB, so
    // flat gains bypass bit-identically at every position.
    //
    // NOT AN EMULATION. Behavioural design from published specification;
    // position names are functional and never model numbers.
    od::Option mCharacter{"Char", 1};
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
      if (ch < 1) ch = 1; else if (ch > 3) ch = 3;

      // Per-position curve law, range, topology and saturation. A table
      // rather than a chain of conditionals, so adding a position is a
      // row and nothing in the sample loop ever branches on it.
      static const ParametricBandQLaw kLaw[3] = {
          PARAM_BAND_Q_CONSTANT, PARAM_BAND_Q_PINNED, PARAM_BAND_Q_CONSTANT };
      static const float kMaxBoost[3] = { 15.0f, 12.0f, 13.5f };
      static const float kMaxCut[3]   = { 15.0f, 12.0f, 17.5f };
      // Saturation is now a CONSEQUENCE of position, not the thing
      // being selected, so it is lighter than the old ladder. Heavy
      // saturation on Passive was measured swamping its own asymmetric
      // range: at a -20 dB request it cut only -5.99 dB against
      // Console's -11.04, because saturating the tap reduces what is
      // subtracted. The curve law and topology carry the character.
      static const float kDrive[3]    = { 0.15f, 0.4f, 0.55f };
      // Passive widens every band: a passive LC section cannot be made
      // surgical, and the broad overlapping curves are the point.
      static const float kQScale[3]   = { 1.0f, 1.0f, 0.55f };
      const ParametricBandQLaw law = kLaw[ch - 1];
      const float maxBoost = kMaxBoost[ch - 1];
      const float maxCut = kMaxCut[ch - 1];
      const float drive = kDrive[ch - 1];
      const float qScale = kQScale[ch - 1];
      const bool parallel = (ch == 3);

      // Coefficients are baked ONCE PER BLOCK, never per sample: the
      // bake contains tan(), pow() and a divide.
      // feedback_runtime_branched_dsp_dispatch - the Colour and shape
      // options are coefficient decisions here, not sample-loop
      // branches.
      const ParametricBandShape lfShape =
          (mLfShape.value() == 2) ? PARAM_BAND_BELL : PARAM_BAND_LOW_SHELF;
      const ParametricBandShape hfShape =
          (mHfShape.value() == 2) ? PARAM_BAND_BELL : PARAM_BAND_HIGH_SHELF;

      // Asymmetric range. Boost and cut are NOT mirror images on a
      // passive design; the published figures are +13.5 against -17.5.
      #define EQ_CLAMP_DB(v) ((v) > maxBoost ? maxBoost : ((v) < -maxCut ? -maxCut : (v)))
      parametricBandBake(mC[0], CLAMP(30.0f, 450.0f, mLfFreq.value()),
                         EQ_CLAMP_DB(mLfGain.value()), 0.7 * qScale,
                         drive, sr, lfShape, law);
      parametricBandBake(mC[1], CLAMP(200.0f, 2500.0f, mLmfFreq.value()),
                         EQ_CLAMP_DB(mLmfGain.value()),
                         CLAMP(0.3f, 10.0f, mLmfQ.value()) * qScale,
                         drive, sr, PARAM_BAND_BELL, law);
      parametricBandBake(mC[2], CLAMP(600.0f, 7000.0f, mHmfFreq.value()),
                         EQ_CLAMP_DB(mHmfGain.value()),
                         CLAMP(0.3f, 10.0f, mHmfQ.value()) * qScale,
                         drive, sr, PARAM_BAND_BELL, law);
      parametricBandBake(mC[3], CLAMP(1500.0f, 16000.0f, mHfFreq.value()),
                         EQ_CLAMP_DB(mHfGain.value()), 0.7 * qScale,
                         drive, sr, hfShape, law);
      #undef EQ_CLAMP_DB

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
