// house::RotCoat
//
// First original-design reverb in the house package. Multi-world
// 4x4 diff-Householder FDN with per-line undersample rates that
// fan around a base World rate. The signature character: each
// FDN line lives in its own "world" (its own divisor/Bezier
// quantization), and the Householder cross-feeds the four
// worlds. Mulch controls how wide the fan opens.
//
// At Mulch=0 all four lines share the same World rate (= a
// classic single-rate FDN). At Mulch=1 the lines fan log-
// symmetrically: slowest line ~1.67x more undersampled
// (stair-steppier reconstruction artifacts), fastest line ~1.67x
// less undersampled (smoother). Cross-line feedback mixes the
// four reconstruction characters into one wash.
//
// NAMING: "RotCoat" is the working CODENAME. Per
// feedback_no_third_party_branding the open-source-faithful-port
// naming exception only applies to literal AW ports; this is
// original work and needs a habitat-native name (Lath / Cure /
// Sediment / Patina / other). Rename is mechanical at release.
//
// PHASE 1 HYBRID FLOAT from start:
//   - FDN line buffers + predelay buffers: float
//   - Per-line feedback taps: float
//   - cyclePhase, cycleStep, inAccum, cycleOut/Prev: double
//     (slow accumulators where precision matters)
//   - Block-rate scalars: double
//
// MACRO TOPOLOGY:
//   in
//     -> Predelay (host rate)
//     -> Per-line accumulate into 4 input buffers (one per line)
//     -> Per-line cyclePhase[i] += cycleStep[i] each sample
//     -> When cyclePhase[i] >= 1: fire line i
//          (write input + feedback into FDN line, advance count,
//           read tap, update recentTap[i], compute Householder
//           for THIS line using recentTaps[0..3], spiral-saturate
//           into fb[i], set cycleOut[i] = recentTap[i])
//     -> Per-sample output: sum of 4 line-interpolations / 4
//     -> Wet/dry crossfade -> out
//
// PER-LINE CYCLESTEP FAN:
//   baseStep = 1.0 / worldRate
//   spreadFactor[i] = {1+0.4*mulch, 1+0.15*mulch,
//                      1-0.15*mulch, 1-0.4*mulch}
//   cycleStep[i] = baseStep * spreadFactor[i]
//   (line 0 = slowest, line 3 = fastest)
//
// FEEDBACK GOVERNOR:
//   Each line's Householder output runs through Spiral (densityA=1)
//   THEN a per-line one-pole lowpass before becoming the new
//   feedback value:
//     - Spiral bounds magnitude to ~[-1, 1] (prevents runaway clip)
//     - LP (target fc=500Hz, alpha scales with worldRate to hold
//       the corner constant in Hz across World stops) keeps the
//       feedback loop bass-dominated. Without this LP the loop
//       carries the full spectrum and high frequencies accumulate
//       indefinitely, becoming snarly and HF-dominant. With the
//       LP the tail is the "lows recirculate" character (saturated
//       low-mid bloom) that's musically usable at every setting.
//
// WORLD=1 + HIGH MULCH CHARACTER:
//   At World=1 the base cycleStep = 1.0 (line fires every host
//   sample). With Mulch high, the slow-line spreadFactors push
//   their cycleSteps ABOVE 1.0 (e.g. 1.4 at full Mulch). When a
//   line's cycleStep > 1.0, cyclePhase advance > 1 per sample.
//   The fire path uses a `while` loop (not `if`) so cyclePhase
//   always settles back into [0, 1) even when cycleStep > 1.0;
//   without the while-loop the phase grows unbounded and the
//   per-sample linear-interp `prev + (out-prev)*phase`
//   EXTRAPOLATES beyond the cycle output, amplifying the signal
//   sample-over-sample and producing a snarling crushed
//   character. The while-loop contains that gain runaway. The
//   resulting World=1 + high-Mulch sound is still distinctly
//   aggressive (multiple lines firing slower than host rate
//   while others fire at host rate = inherent rate-mismatch
//   character) but bounded in level. Worth knowing about as a
//   sound-design corner of the unit.
//
// WORLD SNAP:
//   World param maps to candidate worldRate in [1.0, 8.0], then
//   snaps to nearest of {1, 2, 3, 4, 6, 8}. Non-uniform stops.
//   Always snapped -- no Drift mode (removed; static stops are
//   the intended character).
//
// PARAMETERS (5 total, all continuous, CV-controllable):
//   World    -> snapped base undersample divisor
//   Regen    -> per-line feedback amount [0.1, 0.6]
//   Predelay -> tap offset (0..~170ms at 48k)
//   Mulch    -> per-line cycleStep fan width [0, 1]
//   Wetness  -> standard crossfade wet/dry

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "Spiral.h"

namespace house
{

  // FDN line sizes (primes). At World=1: tap delays span 3.5..35ms.
  // At World=8: span 28..285ms. Range covers closet to small hall.
  static const int kRcI = 1709;
  static const int kRcJ = 911;
  static const int kRcK = 433;
  static const int kRcL = 167;

  // Predelay buffer length: ~170ms at 48k.
  static const int kRcPredelay = 8192;

  class RotCoat : public od::Object
  {
  public:
    RotCoat()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mWorld);
      addParameter(mRegen);
      addParameter(mPredelay);
      addParameter(mMulch);
      addParameter(mWetness);

      memset(fdnIL, 0, sizeof(fdnIL)); memset(fdnIR, 0, sizeof(fdnIR));
      memset(fdnJL, 0, sizeof(fdnJL)); memset(fdnJR, 0, sizeof(fdnJR));
      memset(fdnKL, 0, sizeof(fdnKL)); memset(fdnKR, 0, sizeof(fdnKR));
      memset(fdnLL, 0, sizeof(fdnLL)); memset(fdnLR, 0, sizeof(fdnLR));
      memset(pdlyL, 0, sizeof(pdlyL)); memset(pdlyR, 0, sizeof(pdlyR));

      for (int i = 0; i < 4; i++)
      {
        cyclePhase[i] = 0.0;
        cycleOutL[i] = cycleOutR[i] = 0.0;
        cyclePrevL[i] = cyclePrevR[i] = 0.0;
        inAccumL[i] = inAccumR[i] = 0.0;
        inAccumCount[i] = 0;
        cycleStep[i] = 0.0;
        recentTapL[i] = recentTapR[i] = 0.0f;
      }

      fbAL = fbBL = fbCL = fbDL = 0.0f;
      fbAR = fbBR = fbCR = fbDR = 0.0f;

      for (int i = 0; i < 4; i++)
      {
        fbIirL[i] = 0.0;
        fbIirR[i] = 0.0;
      }

      countI = countJ = countK = countL = 1;
      countP = 0;

      // Set up line pointer arrays so fireLine() can index uniformly.
      // Safe because fdnXL/XR are member arrays with stable addresses.
      mLinesL[0] = fdnIL; mLinesL[1] = fdnJL; mLinesL[2] = fdnKL; mLinesL[3] = fdnLL;
      mLinesR[0] = fdnIR; mLinesR[1] = fdnJR; mLinesR[2] = fdnKR; mLinesR[3] = fdnLR;
      mLineSizes[0] = kRcI; mLineSizes[1] = kRcJ; mLineSizes[2] = kRcK; mLineSizes[3] = kRcL;
      mLineCounts[0] = &countI; mLineCounts[1] = &countJ;
      mLineCounts[2] = &countK; mLineCounts[3] = &countL;
      mLineFbL[0] = &fbAL; mLineFbL[1] = &fbBL; mLineFbL[2] = &fbCL; mLineFbL[3] = &fbDL;
      mLineFbR[0] = &fbAR; mLineFbR[1] = &fbBR; mLineFbR[2] = &fbCR; mLineFbR[3] = &fbDR;
    }

    virtual ~RotCoat() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mWorld{"World", 0.5f};
    od::Parameter mRegen{"Regen", 0.5f};
    od::Parameter mPredelay{"Predelay", 0.0f};
    od::Parameter mMulch{"Mulch", 0.0f};
    od::Parameter mWetness{"Wetness", 0.5f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      const float worldKnob = mWorld.value();
      const float regenKnob = mRegen.value();
      const float predelayKnob = mPredelay.value();
      const float mulchKnob = mMulch.value();
      const float wetnessKnob = mWetness.value();

      // ----- Block-rate worldRate (snap) -----
      double candidate = 1.0 + (double)worldKnob * 7.0;
      double worldRate = snapToWorld(candidate);
      double baseStep = 1.0 / worldRate;

      // ----- Per-line cycleStep fan (Mulch) -----
      // Spread factors centered on 1.0, log-symmetric in spirit.
      // Mulch=0 -> all four = baseStep. Mulch=1 -> spread ~[0.6,
      // 1.4] -> rate ratios ~[0.71, 1.67] around base.
      double m = (double)mulchKnob;
      double spread[4] = {
        1.0 + 0.4 * m,   // line 0: slowest (most undersampled)
        1.0 + 0.15 * m,  // line 1: slightly slower
        1.0 - 0.15 * m,  // line 2: slightly faster
        1.0 - 0.4 * m    // line 3: fastest (least undersampled)
      };
      for (int i = 0; i < 4; i++) cycleStep[i] = baseStep * spread[i];

      // ----- Other block-rate scalars -----
      double regen = 0.1 + ((double)regenKnob * 0.5);

      // Feedback LP coefficient. Target ~500Hz cutoff; alpha
      // scales with worldRate so the LP's effective cutoff stays
      // roughly constant in Hz across World stops (the LP runs at
      // the reduced cycle rate, which is sampleRate / worldRate).
      // Clamp to a stable one-pole range.
      const double kFbLpHz = 500.0;
      double lpAlpha = (2.0 * 3.141592653589793 * kFbLpHz * worldRate)
                       / (double)globalConfig.sampleRate;
      if (lpAlpha > 0.95) lpAlpha = 0.95;
      if (lpAlpha < 0.001) lpAlpha = 0.001;

      int predelayLen = (int)((double)predelayKnob * (double)(kRcPredelay - 1));
      if (predelayLen < 0) predelayLen = 0;
      if (predelayLen >= kRcPredelay) predelayLen = kRcPredelay - 1;

      double wet = (double)wetnessKnob;
      if (wet < 0.0) wet = 0.0;
      if (wet > 1.0) wet = 1.0;
      double dry = 1.0 - wet;

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        double inputSampleL = *in1;
        double inputSampleR = *in2;
        if (fabs(inputSampleL) < 1.18e-23) inputSampleL = 1.18e-17;
        if (fabs(inputSampleR) < 1.18e-23) inputSampleR = 1.18e-17;
        double drySampleL = inputSampleL;
        double drySampleR = inputSampleR;

        // ---- Predelay (host rate, shared by all lines) ----
        pdlyL[countP] = (float)inputSampleL;
        pdlyR[countP] = (float)inputSampleR;
        int pdlyTap = countP - predelayLen;
        if (pdlyTap < 0) pdlyTap += kRcPredelay;
        double predL = (double)pdlyL[pdlyTap];
        double predR = (double)pdlyR[pdlyTap];
        countP++;
        if (countP >= kRcPredelay) countP = 0;

        // ---- Per-line accumulate + phase advance + fire ----
        for (int i = 0; i < 4; i++)
        {
          inAccumL[i] += predL;
          inAccumR[i] += predR;
          inAccumCount[i]++;

          cyclePhase[i] += cycleStep[i];
          // while-loop (not if) handles cycleStep > 1.0 case --
          // at World=1 with Mulch high, slow lines can have
          // cycleStep up to ~1.4, and a single -=1.0 would leave
          // phase >= 1.0, letting per-sample interp extrapolate.
          // The while-loop ensures cyclePhase always settles into
          // [0, 1). cycleStep max is ~1.4 so at most 2 fires per
          // sample -- bounded.
          while (cyclePhase[i] >= 1.0)
          {
            cyclePhase[i] -= 1.0;
            cyclePrevL[i] = cycleOutL[i];
            cyclePrevR[i] = cycleOutR[i];
            fireLine(i, regen, lpAlpha);
          }
        }

        // ---- Sum per-line interpolated outputs ----
        double sumL = 0.0, sumR = 0.0;
        for (int i = 0; i < 4; i++)
        {
          double t = cyclePhase[i];
          sumL += cyclePrevL[i] + (cycleOutL[i] - cyclePrevL[i]) * t;
          sumR += cyclePrevR[i] + (cycleOutR[i] - cyclePrevR[i]) * t;
        }
        double wetL = sumL * 0.25;
        double wetR = sumR * 0.25;

        // ---- Wet/dry crossfade ----
        double outL = wetL * wet + drySampleL * dry;
        double outR = wetR * wet + drySampleR * dry;

        *out1 = (float)outL;
        *out2 = (float)outR;
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    // Snap a continuous candidate worldRate value to the nearest
    // of {1, 2, 3, 4, 6, 8}. Non-uniform per design intent.
    static double snapToWorld(double v)
    {
      static const double stops[6] = {1.0, 2.0, 3.0, 4.0, 6.0, 8.0};
      int best = 0;
      double bestDist = fabs(v - stops[0]);
      for (int i = 1; i < 6; i++)
      {
        double d = fabs(v - stops[i]);
        if (d < bestDist) { bestDist = d; best = i; }
      }
      return stops[best];
    }

    // Fire one line's reduced-rate cycle. Line index in [0, 3]
    // selects which FDN line buffer / count / feedback we touch.
    // Householder reduction uses the most-recent taps stored
    // across all 4 lines (some slightly stale -- musically OK).
    void fireLine(int i, double regen, double lpAlpha)
    {
      // Average input across samples since this line last fired.
      double avgL = (inAccumCount[i] > 0) ? (inAccumL[i] / (double)inAccumCount[i]) : 0.0;
      double avgR = (inAccumCount[i] > 0) ? (inAccumR[i] / (double)inAccumCount[i]) : 0.0;
      inAccumL[i] = 0.0;
      inAccumR[i] = 0.0;
      inAccumCount[i] = 0;

      // Per-line resolved pointers.
      float *lineL = mLinesL[i];
      float *lineR = mLinesR[i];
      int lineSize = mLineSizes[i];
      int *countPtr = mLineCounts[i];
      float *fbLPtr = mLineFbL[i];
      float *fbRPtr = mLineFbR[i];

      // Write input + feedback into line.
      int c = *countPtr;
      lineL[c] = (float)(avgL + (double)(*fbLPtr) * regen);
      lineR[c] = (float)(avgR + (double)(*fbRPtr) * regen);

      // Advance count, wrap.
      c++;
      if (c < 0 || c >= lineSize) c = 0;
      *countPtr = c;

      // Read tap using the standard wrap formula. The lineSize-1
      // index is the maximum valid index; using lineSize-1 as the
      // "delay" gives us the FIFO behavior we want.
      // (delay = lineSize - 1 = maximum available delay)
      int delay = lineSize - 1;
      int tapIdx = c - delay;
      if (tapIdx < 0) tapIdx += lineSize;
      double tapL = (double)lineL[tapIdx];
      double tapR = (double)lineR[tapIdx];

      // Store as the most-recent tap for this line.
      recentTapL[i] = (float)tapL;
      recentTapR[i] = (float)tapR;

      // This line's cycle output IS its tap (the per-line output
      // signal). Per-sample output combiner sums all four.
      cycleOutL[i] = tapL;
      cycleOutR[i] = tapR;

      // Householder 4x4 diff for THIS line:
      //   out_i = tap_i - sum(other 3 taps)
      //         = 2 * tap_i - sum_all
      double sumAllL = (double)recentTapL[0] + (double)recentTapL[1]
                     + (double)recentTapL[2] + (double)recentTapL[3];
      double sumAllR = (double)recentTapR[0] + (double)recentTapR[1]
                     + (double)recentTapR[2] + (double)recentTapR[3];
      double hL = 2.0 * tapL - sumAllL;
      double hR = 2.0 * tapR - sumAllR;

      // Spiral soft-clip (bounds to ~[-1, 1]) then per-line
      // one-pole LP. The LP is what restores the "lows recirc"
      // character that the band-split used to provide -- without
      // it the feedback loop carries full spectrum and HFs
      // accumulate indefinitely (snarly tail). With the LP the
      // tail is bass-dominated and musically usable at every
      // World/Mulch combination.
      double satL = spiralSaturate(hL, 1.0);
      double satR = spiralSaturate(hR, 1.0);
      fbIirL[i] = fbIirL[i] * (1.0 - lpAlpha) + satL * lpAlpha;
      fbIirR[i] = fbIirR[i] * (1.0 - lpAlpha) + satR * lpAlpha;
      *fbLPtr = (float)fbIirL[i];
      *fbRPtr = (float)fbIirR[i];
    }

  private:
    // FDN line buffers (4 lines x 2 sides).
    float fdnIL[kRcI]; float fdnIR[kRcI];
    float fdnJL[kRcJ]; float fdnJR[kRcJ];
    float fdnKL[kRcK]; float fdnKR[kRcK];
    float fdnLL[kRcL]; float fdnLR[kRcL];

    // Predelay buffers (host rate, shared by all lines).
    float pdlyL[kRcPredelay];
    float pdlyR[kRcPredelay];

    // Per-line feedback taps (one per line per side).
    float fbAL, fbBL, fbCL, fbDL;
    float fbAR, fbBR, fbCR, fbDR;

    // Per-line one-pole LP state on feedback path (lows-recirc
    // character). Double for precision in long-decay tail.
    double fbIirL[4];
    double fbIirR[4];

    // FDN line write counters + predelay counter.
    int countI, countJ, countK, countL;
    int countP;

    // Per-line undersample shell state (4 lines).
    double cyclePhase[4];
    double cycleOutL[4], cycleOutR[4];
    double cyclePrevL[4], cyclePrevR[4];
    double inAccumL[4], inAccumR[4];
    int inAccumCount[4];
    double cycleStep[4];           // block-rate baked
    float recentTapL[4], recentTapR[4];  // most-recent tap per line

    // Uniform-access pointer arrays (constructor sets these).
    float *mLinesL[4];
    float *mLinesR[4];
    int mLineSizes[4];
    int *mLineCounts[4];
    float *mLineFbL[4];
    float *mLineFbR[4];
#endif
  };

} // namespace house
