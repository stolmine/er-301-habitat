// house::Lacquer
//
// Second chain-as-unit in the house package. Mixed-rate
// character processor: a Console0-saturated input drives a
// DOWNSAMPLE shell holding Cojones (gritty trajectory
// distortion at reduced rate, aliasing IS the lo-fi character),
// reconstructs to host rate, then enters a 2x UPSAMPLE bracket
// holding TapeFat (clean averaging at oversampled rate,
// "polished" smoothing). The contrast between the lacquer-cut
// rough side and the polished playback side IS the unit's
// identity.
//
// MONOLITHIC by necessity (per planning/lacquer-port-plan.md):
// rate brackets can't graph-compose at host rate, so the chain
// (Console0 + downsample shell + Cojones + upsample bracket +
// TapeFat + Console0 + ChainMix) ships as a single C++ Object
// with everything inlined. The atom architecture's chain-as-Lua
// pattern doesn't extend to mixed-rate chains; this is the
// pragmatic compromise.
//
// PHASE 1 HYBRID FLOAT:
//   - TapeFat circular buffers as `int` (AW fixed-point pattern;
//     small enough that bit-precision matters for the tap-sum)
//   - All other DSP math as `double` (precision-critical state
//     filters + character processing)
//   - I/O as `float` (od::Object buffer convention)
//
// PARAMETERS (4 plies, all continuous with standard coarse/fine
// knob stepping; no hard snap):
//   Drive  - Console0Channel/Bus gain (symmetric, transparent at 0.5)
//   Cut    - continuous worldRate 1..8 (cycleStep 1..0.125) AND
//            Cojones disparity scalar 0.5..3.0. Downsample shell
//            handles fractional worldRate naturally via the
//            cyclePhase accumulator. Character morphs smoothly
//            from clean-ish (Cut=0, host rate, mild honk) to
//            heavily-aliased (Cut=1, ÷8, strong honk on heavily-
//            averaged signal).
//   Polish - continuous TapeFat fatness 3..32 taps (int floor of
//            knob-mapped value) AND wet blend 0.2..1.0. Fatness
//            transitions are subtle 1-tap jumps that the
//            continuous wet blend smooths over musically.
//   Mix    - continuous dry/wet (default 1.0 = full wet)
//
// AW SCALAR REMAPS APPLIED (per feedback_aw_param_default_subtle):
// Both Cojones and TapeFat would reduce to identity at their
// AW defaults. We bias them so the user-facing Cut + Polish
// knob travel stays in the characterful regime end-to-end.
//   Cojones: fixed body=0.3 + breathy=0.2; cojones-disparity
//            multiplier scales with Cut
//   TapeFat: fixed leanfat=positive (always "fat" / lowpass,
//            never "lean" / highpass); fatness + wet both scale
//            with Polish
//
// LOAD-BEARING design invariants (per the plan):
//   1. Cojones inside DOWNSAMPLE shell, TapeFat inside UPSAMPLE
//      bracket — swapping reverses the lacquer-cut metaphor
//   2. Console0 sat pair wraps the WHOLE rate-bracket chain
//   3. 2x decimation MUST have one-pole IIR LPF before sample-
//      drop (otherwise upsample bracket sounds like raw aliasing
//      instead of "polished")
//   4. ChainMix at the OUTPUT (dry path is raw input, wet is
//      post-everything)
//   5. Cojones character math preserves body=0.3 + breathy=0.2
//      defaults — these are non-zero so the unit isn't identity
//      even at Cut=0 (lowest character)
//   6. TapeFat always in "fat" direction; never "lean" (would
//      give highpass / exciter character, contradicts Lacquer
//      premise)
//
// Dropped per template:
//   - Per-sample 32-bit dither
//   - fpd RNG (replaced by deterministic 1.18e-17 denormal flush
//     constant)

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace house
{

  class Lacquer : public od::Object
  {
  public:
    Lacquer()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mDrive);
      addParameter(mCut);
      addParameter(mPolish);
      addParameter(mMix);

      mConsoleChAvgAL = mConsoleChAvgAR = mConsoleChAvgBL = mConsoleChAvgBR = 0.0;
      mConsoleBsAvgAL = mConsoleBsAvgAR = mConsoleBsAvgBL = mConsoleBsAvgBR = 0.0;

      mCyclePhase = 0.0;
      mCycleOutL = mCycleOutR = 0.0;
      mCyclePrevL = mCyclePrevR = 0.0;
      mInAccumL = mInAccumR = 0.0;
      mInAccumCount = 0;

      mStoredL[0] = mStoredL[1] = 0.0;
      mStoredR[0] = mStoredR[1] = 0.0;
      for (int i = 0; i < 6; i++)
      {
        mDiffL[i] = 0.0;
        mDiffR[i] = 0.0;
      }

      mPrevHostL = mPrevHostR = 0.0;
      mDecimateLPL = mDecimateLPR = 0.0;

      memset(mPL, 0, sizeof(mPL));
      memset(mPR, 0, sizeof(mPR));
      mGcount = 128;
    }

    virtual ~Lacquer() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mDrive{"Drive", 0.5f};
    od::Parameter mCut{"Cut", 0.5f};       // default → position 3 → ÷4 + cojones=2.0
    od::Parameter mPolish{"Polish", 0.5f}; // default → position 3 → fatness 16 + wet 0.68
    od::Parameter mMix{"Mix", 1.0f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      // ===== Block-rate scalar baking =====

      // Drive: continuous, unity at 0.5 (matches TickerTape's Console0 pair)
      double drive = (double)mDrive.value();
      if (drive < 0.0) drive = 0.0;
      if (drive > 1.0) drive = 1.0;
      double driveGain = 0.05 + drive * 1.9;

      // Cut: continuous knob 0..1 → continuous worldRate 1..8.
      // Downsample shell handles fractional worldRate via cyclePhase.
      // Cojones disparity scalar scales continuously with Cut (per
      // feedback_aw_param_default_subtle remap — body+breathy stay
      // fixed at non-identity so the atom isn't transparent at Cut=0).
      double cutKnob = (double)mCut.value();
      if (cutKnob < 0.0) cutKnob = 0.0;
      if (cutKnob > 1.0) cutKnob = 1.0;
      double worldRate = 1.0 + cutKnob * 7.0;          // [1.0, 8.0]
      double cycleStep = 1.0 / worldRate;
      double cojonesScalar = 0.5 + cutKnob * 2.5;      // [0.5, 3.0]
      const double kBodyScalar = 0.3;
      const double kBreathyScalar = 0.2;

      // Polish: continuous knob 0..1 → fatness 3..32 (int) AND
      // continuous wet blend 0.2..1.0. Fatness is intrinsically
      // discrete (used in a switch dispatch) but stepped by 1 across
      // 30 positions which feels smooth; wet blend is fully continuous
      // and smooths the audible transition between fatness levels.
      double polishKnob = (double)mPolish.value();
      if (polishKnob < 0.0) polishKnob = 0.0;
      if (polishKnob > 1.0) polishKnob = 1.0;
      int fatness = 3 + (int)(polishKnob * 29.0);      // [3, 32]
      if (fatness < 3) fatness = 3;
      if (fatness > 32) fatness = 32;
      double tapeFatWet = 0.2 + polishKnob * 0.8;      // [0.2, 1.0]
      double tapeFatDry = 1.0 - tapeFatWet;

      // Mix: continuous (the only "always smooth" knob)
      double mix = (double)mMix.value();
      if (mix < 0.0) mix = 0.0;
      if (mix > 1.0) mix = 1.0;
      double oneMinusMix = 1.0 - mix;

      // Decimation LPF alpha — gives roughly ~10kHz cutoff at 96k
      // bracket-rate, which is below 48k Nyquist of host. Anti-alias
      // attenuation of frequencies above ~12kHz before sample-drop.
      const double kDecimateAlpha = 0.5;

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        double inL = *in1;
        double inR = *in2;
        if (fabs(inL) < 1.18e-23) inL = 1.18e-17;
        if (fabs(inR) < 1.18e-23) inR = 1.18e-17;
        double dryL = inL;
        double dryR = inR;

        // ===== Stage 1: Console0Channel sat (input drive) =====

        double tempL = inL;
        inL = (inL + mConsoleChAvgAL) * 0.5;
        mConsoleChAvgAL = tempL;
        double tempR = inR;
        inR = (inR + mConsoleChAvgAR) * 0.5;
        mConsoleChAvgAR = tempR;

        inL *= driveGain;
        inR *= driveGain;

        // BigFastSin (polynomial sat, verbatim from AW Console0Channel)
        if (inL > 1.4137166941154) inL = 1.4137166941154;
        if (inL < -1.4137166941154) inL = -1.4137166941154;
        if (inL > 0.0)
          inL = (inL / 2.0) * (2.8274333882308 - inL);
        else
          inL = -(inL / -2.0) * (2.8274333882308 + inL);

        if (inR > 1.4137166941154) inR = 1.4137166941154;
        if (inR < -1.4137166941154) inR = -1.4137166941154;
        if (inR > 0.0)
          inR = (inR / 2.0) * (2.8274333882308 - inR);
        else
          inR = -(inR / -2.0) * (2.8274333882308 + inR);

        tempL = inL;
        inL = (inL + mConsoleChAvgBL) * 0.5;
        mConsoleChAvgBL = tempL;
        tempR = inR;
        inR = (inR + mConsoleChAvgBR) * 0.5;
        mConsoleChAvgBR = tempR;

        // ===== Stage 2: Downsample shell with Cojones inside =====

        mInAccumL += inL;
        mInAccumR += inR;
        mInAccumCount++;

        mCyclePhase += cycleStep;
        while (mCyclePhase >= 1.0)
        {
          mCyclePhase -= 1.0;
          mCyclePrevL = mCycleOutL;
          mCyclePrevR = mCycleOutR;

          double avgL = (mInAccumCount > 0) ? (mInAccumL / (double)mInAccumCount) : 0.0;
          double avgR = (mInAccumCount > 0) ? (mInAccumR / (double)mInAccumCount) : 0.0;
          mInAccumL = 0.0;
          mInAccumR = 0.0;
          mInAccumCount = 0;

          mCycleOutL = runCojones(avgL, mStoredL, mDiffL,
                                  cojonesScalar, kBodyScalar, kBreathyScalar);
          mCycleOutR = runCojones(avgR, mStoredR, mDiffR,
                                  cojonesScalar, kBodyScalar, kBreathyScalar);
        }

        double shellOutL = mCyclePrevL + (mCycleOutL - mCyclePrevL) * mCyclePhase;
        double shellOutR = mCyclePrevR + (mCycleOutR - mCyclePrevR) * mCyclePhase;

        // ===== Stage 3: 2x upsample bracket with TapeFat inside =====
        //
        // Per host sample: two upsampled samples (linear interp midpoint
        // + current). TapeFat runs at each. Decimation IIR LPF runs at
        // 2x rate; we take the second LPF state as the host-rate output.

        double upSampleAL = (mPrevHostL + shellOutL) * 0.5;
        double upSampleAR = (mPrevHostR + shellOutR) * 0.5;
        double upSampleBL = shellOutL;
        double upSampleBR = shellOutR;

        mPrevHostL = shellOutL;
        mPrevHostR = shellOutR;

        double tapeFatOutAL, tapeFatOutAR;
        runTapeFat(upSampleAL, upSampleAR, fatness, tapeFatWet, tapeFatDry,
                   tapeFatOutAL, tapeFatOutAR);

        double tapeFatOutBL, tapeFatOutBR;
        runTapeFat(upSampleBL, upSampleBR, fatness, tapeFatWet, tapeFatDry,
                   tapeFatOutBL, tapeFatOutBR);

        // Decimation IIR LPF applied to BOTH upsampled outputs sequentially,
        // then sampled at the second position = host rate.
        mDecimateLPL = mDecimateLPL * (1.0 - kDecimateAlpha)
                       + tapeFatOutAL * kDecimateAlpha;
        mDecimateLPL = mDecimateLPL * (1.0 - kDecimateAlpha)
                       + tapeFatOutBL * kDecimateAlpha;
        mDecimateLPR = mDecimateLPR * (1.0 - kDecimateAlpha)
                       + tapeFatOutAR * kDecimateAlpha;
        mDecimateLPR = mDecimateLPR * (1.0 - kDecimateAlpha)
                       + tapeFatOutBR * kDecimateAlpha;
        double bracketOutL = mDecimateLPL;
        double bracketOutR = mDecimateLPR;

        // ===== Stage 4: Console0Bus desat (output recovery) =====

        tempL = bracketOutL;
        bracketOutL = (bracketOutL + mConsoleBsAvgAL) * 0.5;
        mConsoleBsAvgAL = tempL;
        tempR = bracketOutR;
        bracketOutR = (bracketOutR + mConsoleBsAvgAR) * 0.5;
        mConsoleBsAvgAR = tempR;

        bracketOutL *= driveGain;
        bracketOutR *= driveGain;

        // BigFastArcSin (rational desat, verbatim from AW Console0Buss)
        if (bracketOutL > 2.8) bracketOutL = 2.8;
        if (bracketOutL < -2.8) bracketOutL = -2.8;
        if (bracketOutL > 0.0)
          bracketOutL = (bracketOutL * 2.0) / (3.0 - bracketOutL);
        else
          bracketOutL = -(bracketOutL * -2.0) / (3.0 + bracketOutL);

        if (bracketOutR > 2.8) bracketOutR = 2.8;
        if (bracketOutR < -2.8) bracketOutR = -2.8;
        if (bracketOutR > 0.0)
          bracketOutR = (bracketOutR * 2.0) / (3.0 - bracketOutR);
        else
          bracketOutR = -(bracketOutR * -2.0) / (3.0 + bracketOutR);

        tempL = bracketOutL;
        bracketOutL = (bracketOutL + mConsoleBsAvgBL) * 0.5;
        mConsoleBsAvgBL = tempL;
        tempR = bracketOutR;
        bracketOutR = (bracketOutR + mConsoleBsAvgBR) * 0.5;
        mConsoleBsAvgBR = tempR;

        // ===== Stage 5: ChainMix dry/wet =====

        double outL = bracketOutL * mix + dryL * oneMinusMix;
        double outR = bracketOutR * mix + dryR * oneMinusMix;

        *out1 = (float)outL;
        *out2 = (float)outR;
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    // Cojones one-sample-at-reduced-rate. Trajectory tracker over 5-sample
    // history; finds the smallest-magnitude average and uses it as the
    // "trajectory" estimate. Three character scalars (body, cojones,
    // breathy) shape which combination of smoothed + disparity + average
    // dominates the output. Math verbatim from AW Cojones source per
    // feedback_identical_means_identical.
    static inline double runCojones(double inputSample,
                                    double *stored, double *diff,
                                    double cojones, double body, double breathy)
    {
      // Shift history
      stored[1] = stored[0];
      stored[0] = inputSample;
      diff[5] = diff[4];
      diff[4] = diff[3];
      diff[3] = diff[2];
      diff[2] = diff[1];
      diff[1] = diff[0];
      diff[0] = stored[0] - stored[1];

      // Progressive averages (cumulative sums then normalize by tap count)
      double avg0 = diff[0] + diff[1];
      double avg1 = avg0 + diff[2];
      double avg2 = avg1 + diff[3];
      double avg3 = avg2 + diff[4];
      double avg4 = avg3 + diff[5];
      avg0 /= 2.0;
      avg1 /= 3.0;
      avg2 /= 4.0;
      avg3 /= 5.0;
      avg4 /= 6.0;

      // Find smallest-magnitude trajectory (the "calmest" recent direction)
      double meanA = diff[0];
      double meanB = diff[0];
      if (fabs(avg4) < fabs(meanB)) { meanA = meanB; meanB = avg4; }
      if (fabs(avg3) < fabs(meanB)) { meanA = meanB; meanB = avg3; }
      if (fabs(avg2) < fabs(meanB)) { meanA = meanB; meanB = avg2; }
      if (fabs(avg1) < fabs(meanB)) { meanA = meanB; meanB = avg1; }
      if (fabs(avg0) < fabs(meanB)) { meanA = meanB; meanB = avg0; }
      double meanOut = (meanA + meanB) / 2.0;
      stored[0] = stored[1] + meanOut;

      // Combine the three character signals
      double output = stored[0] * body;                          // smoothed body
      output += ((inputSample - stored[0]) - avg1) * cojones;    // disparity / honk
      output += avg1 * breathy;                                  // smoothed breath
      return output;
    }

    // TapeFat single-sample (called twice per host sample inside the 2x
    // bracket). Uses AW's int fixed-point arithmetic for the tap-sum
    // (faster int adds, avoids float drift across the sum). Buffer is
    // double-sized (256 entries) for ping-pong indexing without modulo
    // math. fatness selects how many delay taps participate; the switch
    // fall-through accumulates from chosen tap depth down to tap 1
    // (verbatim from AW TapeFat source).
    inline void runTapeFat(double inL, double inR,
                           int fatness, double wet, double dry,
                           double &outL, double &outR)
    {
      if (mGcount < 0 || mGcount > 128) mGcount = 128;
      int count = mGcount;

      // Write to both halves of the ping-pong buffer
      int intInL = (int)(inL * 8388608.0);
      int intInR = (int)(inR * 8388608.0);
      mPL[count + 128] = mPL[count] = intInL;
      mPR[count + 128] = mPR[count] = intInR;

      int sumtotalL = intInL;
      int sumtotalR = intInR;

      // Switch fall-through (verbatim from AW TapeFat — note NO break
      // statements, intentional cascade through smaller tap depths)
      switch (fatness)
      {
        case 32: sumtotalL += mPL[count+127]; sumtotalR += mPR[count+127];
        case 31: sumtotalL += mPL[count+113]; sumtotalR += mPR[count+113];
        case 30: sumtotalL += mPL[count+109]; sumtotalR += mPR[count+109];
        case 29: sumtotalL += mPL[count+107]; sumtotalR += mPR[count+107];
        case 28: sumtotalL += mPL[count+103]; sumtotalR += mPR[count+103];
        case 27: sumtotalL += mPL[count+101]; sumtotalR += mPR[count+101];
        case 26: sumtotalL += mPL[count+97];  sumtotalR += mPR[count+97];
        case 25: sumtotalL += mPL[count+89];  sumtotalR += mPR[count+89];
        case 24: sumtotalL += mPL[count+83];  sumtotalR += mPR[count+83];
        case 23: sumtotalL += mPL[count+79];  sumtotalR += mPR[count+79];
        case 22: sumtotalL += mPL[count+73];  sumtotalR += mPR[count+73];
        case 21: sumtotalL += mPL[count+71];  sumtotalR += mPR[count+71];
        case 20: sumtotalL += mPL[count+67];  sumtotalR += mPR[count+67];
        case 19: sumtotalL += mPL[count+61];  sumtotalR += mPR[count+61];
        case 18: sumtotalL += mPL[count+59];  sumtotalR += mPR[count+59];
        case 17: sumtotalL += mPL[count+53];  sumtotalR += mPR[count+53];
        case 16: sumtotalL += mPL[count+47];  sumtotalR += mPR[count+47];
        case 15: sumtotalL += mPL[count+43];  sumtotalR += mPR[count+43];
        case 14: sumtotalL += mPL[count+41];  sumtotalR += mPR[count+41];
        case 13: sumtotalL += mPL[count+37];  sumtotalR += mPR[count+37];
        case 12: sumtotalL += mPL[count+31];  sumtotalR += mPR[count+31];
        case 11: sumtotalL += mPL[count+29];  sumtotalR += mPR[count+29];
        case 10: sumtotalL += mPL[count+23];  sumtotalR += mPR[count+23];
        case 9:  sumtotalL += mPL[count+19];  sumtotalR += mPR[count+19];
        case 8:  sumtotalL += mPL[count+17];  sumtotalR += mPR[count+17];
        case 7:  sumtotalL += mPL[count+13];  sumtotalR += mPR[count+13];
        case 6:  sumtotalL += mPL[count+11];  sumtotalR += mPR[count+11];
        case 5:  sumtotalL += mPL[count+7];   sumtotalR += mPR[count+7];
        case 4:  sumtotalL += mPL[count+5];   sumtotalR += mPR[count+5];
        case 3:  sumtotalL += mPL[count+3];   sumtotalR += mPR[count+3];
        case 2:  sumtotalL += mPL[count+2];   sumtotalR += mPR[count+2];
        case 1:  sumtotalL += mPL[count+1];   sumtotalR += mPR[count+1];
      }

      // AW source: /(fatness) then +1 then back to float via /8388608
      // (the +1 is a tiny DC bias to prevent denormal accumulation in
      // the int domain; preserved verbatim).
      double floatTotalL = (double)((sumtotalL / fatness) + 1);
      double floatTotalR = (double)((sumtotalR / fatness) + 1);
      floatTotalL /= 8388608.0;
      floatTotalR /= 8388608.0;
      floatTotalL *= wet;
      floatTotalR *= wet;

      // Always in "fat" (positive leanfat) direction per Lacquer's
      // design — never the "lean" / highpass mode. Blend input with
      // averaged signal weighted by (1-wet) and wet respectively.
      outL = inL * dry + floatTotalL;
      outR = inR * dry + floatTotalR;

      mGcount--;
    }

  private:
    // Console0Channel sat state (4 averaging filter prev-samples).
    double mConsoleChAvgAL, mConsoleChAvgAR, mConsoleChAvgBL, mConsoleChAvgBR;

    // Console0Bus desat state.
    double mConsoleBsAvgAL, mConsoleBsAvgAR, mConsoleBsAvgBL, mConsoleBsAvgBR;

    // Downsample shell state.
    double mCyclePhase;
    double mCycleOutL, mCycleOutR;
    double mCyclePrevL, mCyclePrevR;
    double mInAccumL, mInAccumR;
    int mInAccumCount;

    // Cojones state per side (2-sample stored history + 6-sample diff history).
    double mStoredL[2], mStoredR[2];
    double mDiffL[6], mDiffR[6];

    // 2x upsample bracket state.
    double mPrevHostL, mPrevHostR;   // previous host-rate input for midpoint interp
    double mDecimateLPL, mDecimateLPR; // one-pole IIR state for decimation LPF

    // TapeFat circular delay buffers (ping-pong duplicated — 128 unique
    // entries, mirrored to indices 128..255 for wrap-free indexing).
    // AW fixed-point pattern: int summation, scale by 8388608.
    int mPL[256], mPR[256];
    int mGcount;
#endif
  };

} // namespace house
