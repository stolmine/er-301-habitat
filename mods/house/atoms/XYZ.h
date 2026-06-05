// house::XYZ
//
// Second original-design reverb in the house package. Three-axis
// cryptic engine where Z reframes what X and Y mean. Working
// CODENAME -- needs habitat-native rename before any release
// (Cistern / Vault / Crypt / Reliquary / Ley / other) per
// feedback_no_third_party_branding.
//
// PHASE 1 HYBRID FLOAT (memory + CPU):
//   - All FDN line buffers + predelay: float
//   - Per-line feedback taps: float
//   - Per-line LP IIR state: double
//   - Bezier shell accumulators + cyclePhase + cycleOut/Prev: double
//   - Block-rate scalars: double
//
// AXES:
//   X  -> Size + texture morph. Two coupled things move:
//         * FDN line delay length scale [0.2, 1.7] x base size
//         * APF g coefficient (1-X)*0.6 -- heavy diffusion at X=0,
//           pure delay at X=1
//   Y  -> Sat+undersample coupled-curve.
//         * spiralDrive = 1.0 + Y^1.4 * 9.0   range [1, 10]
//         * cycleStep   = 1.0 + Y^2.2 * 3.0   range [1, 4]
//         (exponents from the design doc sketch; listen-tune at
//         Phase D if mid-Y feels mushy)
//   Z  -> Topology switch, snapped continuous to 3 stops:
//         * Z < 0.33  : NESTED  (clean serial: sat -> FDN -> de-sat)
//         * Z < 0.67  : FOLDED  (sat moves INSIDE feedback loop;
//                                per-pass aliasing of generated
//                                harmonics = signature sound)
//         * Z >= 0.67 : COUPLED (two slightly-detuned sub-FDNs
//                                cross-modulating via Spiral-leashed
//                                cross-link; emergent / dramatic)
//   Predelay -> standard 0..170ms at 48k tap.
//   Wetness  -> standard crossfade.
//
// REGEN: implicit in X. regen = 0.1 + X * 0.5 (range [0.1, 0.6]).
// Bigger room (high X) = longer tail. Smaller room = shorter tail.
//
// LOAD-BEARING DESIGN INVARIANTS (per planning/xyz-port-plan.md):
//   1. Folded mode applies sat AND undersample inside the loop;
//      both together produce the per-pass aliasing.
//   2. Coupled mode forces cycleStep >= 2.0 (AM335x compromise).
//   3. Per-line Spiral governor (densityA=1, bounded) applies in
//      ALL Z modes regardless of where the Y-driven sat lives.
//   4. APF formula: nested form -- the FDN line buffer IS the
//      APF's delay line. No extra state needed beyond the line
//      buffer itself.
//   5. Cross-link is additive (each FDN's input gets a portion of
//      the OTHER FDN's previous-cycle tap-sum, Spiral-saturated
//      with Y drive as the leash).
//   6. While-fire on cyclePhase: cycleStep capped at 4.0 in code
//      so max 4 fires per sample. RotCoat lesson.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "Spiral.h"
#include "AllpassMono.h"

namespace house
{

  // ---- FDN A line sizes (max X=1.7 scaled, allocated as primes) ----
  // Base sizes 1709, 911, 433, 167. Max scaled: 2906, 1549, 736, 284.
  // Round up to next prime.
  static const int kXyAI = 2917;
  static const int kXyAJ = 1567;
  static const int kXyAK = 743;
  static const int kXyAL = 293;

  // ---- FDN B line sizes (slightly detuned for Coupled) ----
  // Base sizes 1933, 1009, 461, 191. Max scaled: 3286, 1715, 784, 325.
  static const int kXyBI = 3299;
  static const int kXyBJ = 1721;
  static const int kXyBK = 787;
  static const int kXyBL = 331;

  // Base line sizes (used to compute effective delay = baseSize * X_scale).
  static const int kXyABaseI = 1709;
  static const int kXyABaseJ = 911;
  static const int kXyABaseK = 433;
  static const int kXyABaseL = 167;
  static const int kXyBBaseI = 1933;
  static const int kXyBBaseJ = 1009;
  static const int kXyBBaseK = 461;
  static const int kXyBBaseL = 191;

  static const int kXyPredelay = 8192;

  // Cross-link coupling strength (Coupled mode). Fixed; revisit
  // empirically at Phase C audition.
  static const double kXyCrossGain = 0.3;

  class XYZ : public od::Object
  {
  public:
    XYZ()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mX);
      addParameter(mY);
      addParameter(mZ);
      addParameter(mPredelay);
      addParameter(mWetness);

      memset(fdnA_I_L, 0, sizeof(fdnA_I_L)); memset(fdnA_I_R, 0, sizeof(fdnA_I_R));
      memset(fdnA_J_L, 0, sizeof(fdnA_J_L)); memset(fdnA_J_R, 0, sizeof(fdnA_J_R));
      memset(fdnA_K_L, 0, sizeof(fdnA_K_L)); memset(fdnA_K_R, 0, sizeof(fdnA_K_R));
      memset(fdnA_L_L, 0, sizeof(fdnA_L_L)); memset(fdnA_L_R, 0, sizeof(fdnA_L_R));
      memset(fdnB_I_L, 0, sizeof(fdnB_I_L)); memset(fdnB_I_R, 0, sizeof(fdnB_I_R));
      memset(fdnB_J_L, 0, sizeof(fdnB_J_L)); memset(fdnB_J_R, 0, sizeof(fdnB_J_R));
      memset(fdnB_K_L, 0, sizeof(fdnB_K_L)); memset(fdnB_K_R, 0, sizeof(fdnB_K_R));
      memset(fdnB_L_L, 0, sizeof(fdnB_L_L)); memset(fdnB_L_R, 0, sizeof(fdnB_L_R));
      memset(pdlyL, 0, sizeof(pdlyL));
      memset(pdlyR, 0, sizeof(pdlyR));

      for (int i = 0; i < 4; i++)
      {
        fdnAFbL[i] = fdnAFbR[i] = 0.0f;
        fdnBFbL[i] = fdnBFbR[i] = 0.0f;
        fdnAFbIirL[i] = fdnAFbIirR[i] = 0.0;
        fdnBFbIirL[i] = fdnBFbIirR[i] = 0.0;
        fdnACounts[i] = 1;
        fdnBCounts[i] = 1;
      }

      fdnALastTapSumL = fdnALastTapSumR = 0.0;
      fdnBLastTapSumL = fdnBLastTapSumR = 0.0;
      cyclePhase = 0.0;
      cycleOutL = cycleOutR = 0.0;
      cyclePrevL = cyclePrevR = 0.0;
      inAccumL = inAccumR = 0.0;
      inAccumCount = 0;
      countP = 0;

      // Pointer arrays for uniform per-line access.
      fdnALinesL[0] = fdnA_I_L; fdnALinesR[0] = fdnA_I_R;
      fdnALinesL[1] = fdnA_J_L; fdnALinesR[1] = fdnA_J_R;
      fdnALinesL[2] = fdnA_K_L; fdnALinesR[2] = fdnA_K_R;
      fdnALinesL[3] = fdnA_L_L; fdnALinesR[3] = fdnA_L_R;
      fdnALineSizes[0] = kXyAI;
      fdnALineSizes[1] = kXyAJ;
      fdnALineSizes[2] = kXyAK;
      fdnALineSizes[3] = kXyAL;
      fdnABaseSizes[0] = kXyABaseI;
      fdnABaseSizes[1] = kXyABaseJ;
      fdnABaseSizes[2] = kXyABaseK;
      fdnABaseSizes[3] = kXyABaseL;

      fdnBLinesL[0] = fdnB_I_L; fdnBLinesR[0] = fdnB_I_R;
      fdnBLinesL[1] = fdnB_J_L; fdnBLinesR[1] = fdnB_J_R;
      fdnBLinesL[2] = fdnB_K_L; fdnBLinesR[2] = fdnB_K_R;
      fdnBLinesL[3] = fdnB_L_L; fdnBLinesR[3] = fdnB_L_R;
      fdnBLineSizes[0] = kXyBI;
      fdnBLineSizes[1] = kXyBJ;
      fdnBLineSizes[2] = kXyBK;
      fdnBLineSizes[3] = kXyBL;
      fdnBBaseSizes[0] = kXyBBaseI;
      fdnBBaseSizes[1] = kXyBBaseJ;
      fdnBBaseSizes[2] = kXyBBaseK;
      fdnBBaseSizes[3] = kXyBBaseL;
    }

    virtual ~XYZ() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mX{"X", 0.5f};
    od::Parameter mY{"Y", 0.3f};
    od::Parameter mZ{"Z", 0.5f};      // default Folded (signature sound)
    od::Parameter mPredelay{"Predelay", 0.0f};
    od::Parameter mWetness{"Wetness", 0.5f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      const float xKnob = mX.value();
      const float yKnob = mY.value();
      const float zKnob = mZ.value();
      const float pdelayKnob = mPredelay.value();
      const float wetnessKnob = mWetness.value();

      // ----- Z mode (block-rate) -----
      bool isNested  = zKnob < 0.33f;
      bool isCoupled = zKnob >= 0.67f;
      bool isFolded  = !isNested && !isCoupled;
      (void)isFolded; // referenced via isNested/isCoupled checks below

      // ----- X-derived scalars -----
      double xScale = 0.2 + (double)xKnob * 1.5;   // [0.2, 1.7]
      double apfG = (1.0 - (double)xKnob) * 0.6;   // [0.6, 0]
      double regen = 0.1 + (double)xKnob * 0.5;    // [0.1, 0.6]

      // Per-line effective delays from base * X_scale, clamped to
      // buffer size minus 1.
      int delayA[4], delayB[4];
      for (int i = 0; i < 4; i++)
      {
        int dA = (int)((double)fdnABaseSizes[i] * xScale);
        if (dA < 1) dA = 1;
        if (dA >= fdnALineSizes[i]) dA = fdnALineSizes[i] - 1;
        delayA[i] = dA;
        int dB = (int)((double)fdnBBaseSizes[i] * xScale);
        if (dB < 1) dB = 1;
        if (dB >= fdnBLineSizes[i]) dB = fdnBLineSizes[i] - 1;
        delayB[i] = dB;
      }

      // ----- Y-derived scalars (the coupled-curve axis) -----
      double yClamped = (double)yKnob;
      if (yClamped < 0.0) yClamped = 0.0;
      if (yClamped > 1.0) yClamped = 1.0;
      double spiralDrive = 1.0 + pow(yClamped, 1.4) * 9.0; // [1, 10]
      double cycleStep   = 1.0 + pow(yClamped, 2.2) * 3.0; // [1, 4]
      // Coupled mode: force minimum undersample for AM335x budget.
      if (isCoupled && cycleStep < 2.0) cycleStep = 2.0;
      if (cycleStep > 4.0) cycleStep = 4.0; // safety: bound fires/sample to 4

      // ----- Feedback LP coefficient (block-rate, ~500Hz target)
      // alpha scales with cycleStep so the LP holds its corner in
      // Hz across Y / Coupled cap changes (same RotCoat pattern).
      const double kFbLpHz = 500.0;
      double lpAlpha = (2.0 * 3.141592653589793 * kFbLpHz * cycleStep)
                       / (double)globalConfig.sampleRate;
      if (lpAlpha > 0.95) lpAlpha = 0.95;
      if (lpAlpha < 0.001) lpAlpha = 0.001;

      // ----- Predelay length, wet/dry -----
      int predelayLen = (int)((double)pdelayKnob * (double)(kXyPredelay - 1));
      if (predelayLen < 0) predelayLen = 0;
      if (predelayLen >= kXyPredelay) predelayLen = kXyPredelay - 1;

      double wet = (double)wetnessKnob;
      if (wet < 0.0) wet = 0.0;
      if (wet > 1.0) wet = 1.0;
      double dry = 1.0 - wet;

      // Output gain compensation per mode (Coupled sums 2 FDNs so
      // halve the combined wet to keep perceived loudness similar
      // across modes).
      double outScale = isCoupled ? 0.125 : 0.25;
      (void)outScale; // applied directly in fire path

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        double inputSampleL = *in1;
        double inputSampleR = *in2;
        if (fabs(inputSampleL) < 1.18e-23) inputSampleL = 1.18e-17;
        if (fabs(inputSampleR) < 1.18e-23) inputSampleR = 1.18e-17;
        double drySampleL = inputSampleL;
        double drySampleR = inputSampleR;

        // ---- Predelay (host rate) ----
        pdlyL[countP] = (float)inputSampleL;
        pdlyR[countP] = (float)inputSampleR;
        int pdlyTap = countP - predelayLen;
        if (pdlyTap < 0) pdlyTap += kXyPredelay;
        double predL = (double)pdlyL[pdlyTap];
        double predR = (double)pdlyR[pdlyTap];
        countP++;
        if (countP >= kXyPredelay) countP = 0;

        // ---- Nested mode pre-sat (sat outside the loop) ----
        // Use fast Spiral (5th-order Taylor) for the per-sample
        // path -- libm sin on scalar Cortex-A8 is ~200 cycles,
        // poly is ~10 cycles, and the curve difference is
        // inaudible for a saturator.
        if (isNested)
        {
          predL = spiralFastSaturate(predL, spiralDrive);
          predR = spiralFastSaturate(predR, spiralDrive);
        }

        // ---- Accumulate into Bezier input buffer ----
        inAccumL += predL;
        inAccumR += predR;
        inAccumCount++;

        // ---- Advance cyclePhase, fire when crossed (while-loop) ----
        cyclePhase += cycleStep;
        while (cyclePhase >= 1.0)
        {
          cyclePhase -= 1.0;
          cyclePrevL = cycleOutL;
          cyclePrevR = cycleOutR;
          fireCycle(regen, apfG, lpAlpha, spiralDrive,
                    isCoupled, isNested, delayA, delayB);
        }

        // ---- Linear interp output between cyclePrev and cycleOut ----
        double wetL = cyclePrevL + (cycleOutL - cyclePrevL) * cyclePhase;
        double wetR = cyclePrevR + (cycleOutR - cyclePrevR) * cyclePhase;

        // NOTE: post-de-sat was dropped for CPU reasons (asin is
        // expensive AND doesn't truly invert because the FDN
        // changed the signal between sat and de-sat). Nested mode
        // effectively becomes "saturated wet + clean dry" -- a
        // clearer mental model that costs much less CPU than the
        // 2 asin/sample/channel the inverse would have required.

        // ---- Wet/dry crossfade ----
        double outL = wetL * wet + drySampleL * dry;
        double outR = wetR * wet + drySampleR * dry;

        *out1 = (float)outL;
        *out2 = (float)outR;
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    // Process one cycle. Fires FDN A always; FDN B only if Coupled.
    // Updates cycleOutL/R with the wet sample for this cycle.
    void fireCycle(double regen, double apfG, double lpAlpha,
                   double spiralDrive,
                   bool isCoupled, bool isNested,
                   const int delayA[4], const int delayB[4])
    {
      // Compute average input across samples since last fire.
      double avgL = (inAccumCount > 0) ? (inAccumL / (double)inAccumCount) : 0.0;
      double avgR = (inAccumCount > 0) ? (inAccumR / (double)inAccumCount) : 0.0;
      inAccumL = 0.0;
      inAccumR = 0.0;
      inAccumCount = 0;

      // Cross-link contributions (Coupled mode): each FDN's input
      // adds a Spiral-leashed portion of the OTHER FDN's last
      // tap-sum. Computed once per fire.
      double crossA_L = 0.0, crossA_R = 0.0;
      double crossB_L = 0.0, crossB_R = 0.0;
      if (isCoupled)
      {
        // FDN A sees B's last tap sum (saturated by Y-drive Spiral).
        // Fast Spiral throughout fireCycle -- this gets called
        // many times per sample at low Y / Coupled cap, and the
        // 0.45% curve error is inaudible for a saturator.
        crossA_L = kXyCrossGain * spiralFastSaturate(fdnBLastTapSumL, spiralDrive);
        crossA_R = kXyCrossGain * spiralFastSaturate(fdnBLastTapSumR, spiralDrive);
        // FDN B sees A's last tap sum.
        crossB_L = kXyCrossGain * spiralFastSaturate(fdnALastTapSumL, spiralDrive);
        crossB_R = kXyCrossGain * spiralFastSaturate(fdnALastTapSumR, spiralDrive);
      }

      double sumTapAL = 0.0, sumTapAR = 0.0;
      double sumTapBL = 0.0, sumTapBR = 0.0;

      // ===== FDN A =====
      {
        double tapL[4], tapR[4];
        for (int i = 0; i < 4; i++)
        {
          int c = fdnACounts[i];
          int sz = fdnALineSizes[i];
          int d = delayA[i];
          float *lineL = fdnALinesL[i];
          float *lineR = fdnALinesR[i];

          double xNowL = avgL + (double)fdnAFbL[i] * regen + crossA_L;
          double xNowR = avgR + (double)fdnAFbR[i] * regen + crossA_R;

          // Read delayed v[n-N] from line BEFORE writing v[n].
          int tapIdx = c - d;
          if (tapIdx < 0) tapIdx += sz;
          double vDelayedL = (double)lineL[tapIdx];
          double vDelayedR = (double)lineR[tapIdx];

          // APF nested step: write vNew, output yOut.
          double vNewL, yOutL, vNewR, yOutR;
          allpassNestedStep(xNowL, vDelayedL, apfG, vNewL, yOutL);
          allpassNestedStep(xNowR, vDelayedR, apfG, vNewR, yOutR);

          lineL[c] = (float)vNewL;
          lineR[c] = (float)vNewR;

          // Advance count.
          c++;
          if (c >= sz) c = 0;
          fdnACounts[i] = c;

          tapL[i] = yOutL;
          tapR[i] = yOutR;
        }

        // Householder reduce (out_i = 2*tap_i - sum_all) and feedback path.
        double sumL = tapL[0] + tapL[1] + tapL[2] + tapL[3];
        double sumR = tapR[0] + tapR[1] + tapR[2] + tapR[3];
        sumTapAL = sumL;
        sumTapAR = sumR;

        for (int i = 0; i < 4; i++)
        {
          double hL = 2.0 * tapL[i] - sumL;
          double hR = 2.0 * tapR[i] - sumR;

          // Folded mode: Y-drive Spiral on the in-loop feedback.
          // (Nested + Coupled skip this; their sat lives elsewhere.)
          if (!isNested && !isCoupled)
          {
            hL = spiralFastSaturate(hL, spiralDrive);
            hR = spiralFastSaturate(hR, spiralDrive);
          }

          // Spiral governor (densityA=1, bounded to ~[-1, 1])
          // applies in all modes -- prevents runaway clip.
          double govL = spiralFastSaturate(hL, 1.0);
          double govR = spiralFastSaturate(hR, 1.0);

          // One-pole LP on feedback (lows-recirculate character).
          fdnAFbIirL[i] = fdnAFbIirL[i] * (1.0 - lpAlpha) + govL * lpAlpha;
          fdnAFbIirR[i] = fdnAFbIirR[i] * (1.0 - lpAlpha) + govR * lpAlpha;
          fdnAFbL[i] = (float)fdnAFbIirL[i];
          fdnAFbR[i] = (float)fdnAFbIirR[i];
        }
      }

      // Store FDN A tap sum for cross-link readback next cycle.
      fdnALastTapSumL = sumTapAL;
      fdnALastTapSumR = sumTapAR;

      // ===== FDN B (Coupled only) =====
      if (isCoupled)
      {
        double tapL[4], tapR[4];
        for (int i = 0; i < 4; i++)
        {
          int c = fdnBCounts[i];
          int sz = fdnBLineSizes[i];
          int d = delayB[i];
          float *lineL = fdnBLinesL[i];
          float *lineR = fdnBLinesR[i];

          double xNowL = avgL + (double)fdnBFbL[i] * regen + crossB_L;
          double xNowR = avgR + (double)fdnBFbR[i] * regen + crossB_R;

          int tapIdx = c - d;
          if (tapIdx < 0) tapIdx += sz;
          double vDelayedL = (double)lineL[tapIdx];
          double vDelayedR = (double)lineR[tapIdx];

          double vNewL, yOutL, vNewR, yOutR;
          allpassNestedStep(xNowL, vDelayedL, apfG, vNewL, yOutL);
          allpassNestedStep(xNowR, vDelayedR, apfG, vNewR, yOutR);

          lineL[c] = (float)vNewL;
          lineR[c] = (float)vNewR;

          c++;
          if (c >= sz) c = 0;
          fdnBCounts[i] = c;

          tapL[i] = yOutL;
          tapR[i] = yOutR;
        }

        double sumL = tapL[0] + tapL[1] + tapL[2] + tapL[3];
        double sumR = tapR[0] + tapR[1] + tapR[2] + tapR[3];
        sumTapBL = sumL;
        sumTapBR = sumR;

        // FDN B is in Coupled mode by definition here -- skip
        // Folded in-loop sat block.
        for (int i = 0; i < 4; i++)
        {
          double hL = 2.0 * tapL[i] - sumL;
          double hR = 2.0 * tapR[i] - sumR;

          double govL = spiralFastSaturate(hL, 1.0);
          double govR = spiralFastSaturate(hR, 1.0);

          fdnBFbIirL[i] = fdnBFbIirL[i] * (1.0 - lpAlpha) + govL * lpAlpha;
          fdnBFbIirR[i] = fdnBFbIirR[i] * (1.0 - lpAlpha) + govR * lpAlpha;
          fdnBFbL[i] = (float)fdnBFbIirL[i];
          fdnBFbR[i] = (float)fdnBFbIirR[i];
        }

        fdnBLastTapSumL = sumTapBL;
        fdnBLastTapSumR = sumTapBR;
      }

      // ===== Cycle output =====
      // Sum both FDNs (or just A) and scale per mode.
      if (isCoupled)
      {
        cycleOutL = (sumTapAL + sumTapBL) * 0.125; // sum / 8
        cycleOutR = (sumTapAR + sumTapBR) * 0.125;
      }
      else
      {
        cycleOutL = sumTapAL * 0.25; // sum / 4
        cycleOutR = sumTapAR * 0.25;
      }
    }

  private:
    // FDN A line buffers (4 lines x 2 sides, allocated at max-X size).
    float fdnA_I_L[kXyAI]; float fdnA_I_R[kXyAI];
    float fdnA_J_L[kXyAJ]; float fdnA_J_R[kXyAJ];
    float fdnA_K_L[kXyAK]; float fdnA_K_R[kXyAK];
    float fdnA_L_L[kXyAL]; float fdnA_L_R[kXyAL];

    // FDN B line buffers (Coupled mode; otherwise dormant).
    float fdnB_I_L[kXyBI]; float fdnB_I_R[kXyBI];
    float fdnB_J_L[kXyBJ]; float fdnB_J_R[kXyBJ];
    float fdnB_K_L[kXyBK]; float fdnB_K_R[kXyBK];
    float fdnB_L_L[kXyBL]; float fdnB_L_R[kXyBL];

    // Predelay (host rate, shared by both FDNs).
    float pdlyL[kXyPredelay];
    float pdlyR[kXyPredelay];

    // Per-line feedback taps (one per FDN, per line, per side).
    float fdnAFbL[4], fdnAFbR[4];
    float fdnBFbL[4], fdnBFbR[4];

    // Per-line feedback LP state.
    double fdnAFbIirL[4], fdnAFbIirR[4];
    double fdnBFbIirL[4], fdnBFbIirR[4];

    // Per-FDN counts (one count per line, shared L+R).
    int fdnACounts[4], fdnBCounts[4];

    // Cross-link tap-sum storage (one per FDN per side).
    double fdnALastTapSumL, fdnALastTapSumR;
    double fdnBLastTapSumL, fdnBLastTapSumR;

    // Shared Bezier undersample shell state.
    double cyclePhase;
    double cycleOutL, cycleOutR;
    double cyclePrevL, cyclePrevR;
    double inAccumL, inAccumR;
    int inAccumCount;

    // Predelay counter.
    int countP;

    // Uniform-access pointer arrays (constructor sets these).
    float *fdnALinesL[4];
    float *fdnALinesR[4];
    int fdnALineSizes[4];
    int fdnABaseSizes[4];
    float *fdnBLinesL[4];
    float *fdnBLinesR[4];
    int fdnBLineSizes[4];
    int fdnBBaseSizes[4];
#endif
  };

} // namespace house
