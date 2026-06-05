// house::Verbity
//
// Airwindows Verbity (Chris Johnson, MIT) port. 3-stage cascaded
// 4x4 diff-Householder FDN with input+output IIR lowpass,
// per-feedback-tap interpolation smoother, and a sub-low "thunder"
// chase. Fifth AW atom in the house package. Lightest trap
// profile of any house atom: no transcendentals per sample, no
// modulated reads, no LFOs, no cross-coupling.
//
// PHASE 1 HYBRID FLOAT (post-CreamCoat default + memory-necessary):
//   - 12 FDN delay arrays: `float` (~503 KB stereo at float vs
//     ~1 MB at double; double would blow L2 4x not 2x)
//   - feedback{A..D}{L,R}, previous{A..D}{L,R}: `float` (small
//     magnitudes, regen <= 0.094)
//   - lastRef{L,R}[7]: `float` (per-sample reads)
//   - iir{A,B}{L,R}: `double` (per-sample IIR feedback path,
//     precision-sensitive)
//   - thunder{L,R}: `double` (0.99 IIR accumulator, decay-sensitive
//     across many samples)
//   - Block-rate scalars (size, regen, lowpass, interpolate,
//     thunderAmount, wet, dry): `double`
//
// LOAD-BEARING from AW source (preserved literally per
// feedback_identical_means_identical):
//   - All count* init to 1; cycle init to 0
//   - thunder{L,R} init to 0
//   - Sum/8 combiner (not sum/4) at the FDN output combiner --
//     intentional gain reduction; do NOT "correct" to /4
//   - Submix wet/dry semantics: Wetness=0.5 yields full wet AND
//     full dry summed (NOT crossfaded). For send patches.
//   - Thunder term sign: thunderL -= feedbackAL * thunderAmount
//     (minus, accumulated as negative-going low chase)
//   - Previous-tap update ORDER: feedback gets interpolated first,
//     THEN previous = updated feedback. Reversing the order would
//     break the IIR-like smoother.
//
// Dropped per template:
//   - Per-sample 32-bit dither (frexpf + pow per sample)
//   - rand()-seeded fpdL/fpdR
//   - VST host deps

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace house
{

  // Delay-line sizes (verbatim from AW Verbity.h) -- max value of
  // delay* per stage, used at SIZE=1.0 + size=1.87. Allocate to
  // this max; per-block delay* is clamped via size knob.
  static const int kVbI = 6480;
  static const int kVbJ = 3660;
  static const int kVbK = 1720;
  static const int kVbL = 680;
  static const int kVbA = 9700;
  static const int kVbB = 6000;
  static const int kVbC = 2320;
  static const int kVbD = 940;
  static const int kVbE = 15220;
  static const int kVbF = 8460;
  static const int kVbG = 4540;
  static const int kVbH = 3200;

  class Verbity : public od::Object
  {
  public:
    Verbity()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mBigness);
      addParameter(mLongness);
      addParameter(mDarkness);
      addParameter(mWetness);

      memset(aIL, 0, sizeof(aIL)); memset(aIR, 0, sizeof(aIR));
      memset(aJL, 0, sizeof(aJL)); memset(aJR, 0, sizeof(aJR));
      memset(aKL, 0, sizeof(aKL)); memset(aKR, 0, sizeof(aKR));
      memset(aLL, 0, sizeof(aLL)); memset(aLR, 0, sizeof(aLR));
      memset(aAL, 0, sizeof(aAL)); memset(aAR, 0, sizeof(aAR));
      memset(aBL, 0, sizeof(aBL)); memset(aBR, 0, sizeof(aBR));
      memset(aCL, 0, sizeof(aCL)); memset(aCR, 0, sizeof(aCR));
      memset(aDL, 0, sizeof(aDL)); memset(aDR, 0, sizeof(aDR));
      memset(aEL, 0, sizeof(aEL)); memset(aER, 0, sizeof(aER));
      memset(aFL, 0, sizeof(aFL)); memset(aFR, 0, sizeof(aFR));
      memset(aGL, 0, sizeof(aGL)); memset(aGR, 0, sizeof(aGR));
      memset(aHL, 0, sizeof(aHL)); memset(aHR, 0, sizeof(aHR));
      memset(lastRefL, 0, sizeof(lastRefL));
      memset(lastRefR, 0, sizeof(lastRefR));

      iirAL = iirAR = 0.0;
      iirBL = iirBR = 0.0;
      feedbackAL = feedbackAR = 0.0f;
      feedbackBL = feedbackBR = 0.0f;
      feedbackCL = feedbackCR = 0.0f;
      feedbackDL = feedbackDR = 0.0f;
      previousAL = previousAR = 0.0f;
      previousBL = previousBR = 0.0f;
      previousCL = previousCR = 0.0f;
      previousDL = previousDR = 0.0f;
      thunderL = thunderR = 0.0;

      countA = countB = countC = countD = 1;
      countE = countF = countG = countH = 1;
      countI = countJ = countK = countL = 1;
      cycle = 0;
    }

    virtual ~Verbity() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mBigness{"Bigness", 0.25f};
    od::Parameter mLongness{"Longness", 0.0f};
    od::Parameter mDarkness{"Darkness", 0.25f};
    od::Parameter mWetness{"Wetness", 0.25f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      const float A = mBigness.value();
      const float B = mLongness.value();
      const float C = mDarkness.value();
      const float D = mWetness.value();

      double overallscale = 1.0;
      overallscale /= 44100.0;
      overallscale *= (double)globalConfig.sampleRate;

      int cycleEnd = (int)floor(overallscale);
      if (cycleEnd < 1) cycleEnd = 1;
      if (cycleEnd > 4) cycleEnd = 4;
      if (cycle > cycleEnd - 1) cycle = cycleEnd - 1;

      double size = ((double)A * 1.77) + 0.1;
      double regen = 0.0625 + ((double)B * 0.03125);
      double lowpass = (1.0 - pow((double)C, 2.0)) / sqrt(overallscale);
      double interpolate = pow((double)C, 2.0) * 0.618033988749894848204586;
      double thunderAmount = (0.3 - ((double)B * 0.22)) * (double)C * 0.1;
      double wet = (double)D * 2.0;
      double dry = 2.0 - wet;
      if (wet > 1.0) wet = 1.0;
      if (wet < 0.0) wet = 0.0;
      if (dry > 1.0) dry = 1.0;
      if (dry < 0.0) dry = 0.0;

      delayI = (int)(3407.0 * size);
      delayJ = (int)(1823.0 * size);
      delayK = (int)(859.0 * size);
      delayL = (int)(331.0 * size);
      delayA = (int)(4801.0 * size);
      delayB = (int)(2909.0 * size);
      delayC = (int)(1153.0 * size);
      delayD = (int)(461.0 * size);
      delayE = (int)(7607.0 * size);
      delayF = (int)(4217.0 * size);
      delayG = (int)(2269.0 * size);
      delayH = (int)(1597.0 * size);

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        double inputSampleL = *in1;
        double inputSampleR = *in2;
        if (fabs(inputSampleL) < 1.18e-23) inputSampleL = 1.18e-17;
        if (fabs(inputSampleR) < 1.18e-23) inputSampleR = 1.18e-17;
        double drySampleL = inputSampleL;
        double drySampleR = inputSampleR;

        if (fabs(iirAL) < 1.18e-37) iirAL = 0.0;
        iirAL = (iirAL * (1.0 - lowpass)) + (inputSampleL * lowpass);
        inputSampleL = iirAL;
        if (fabs(iirAR) < 1.18e-37) iirAR = 0.0;
        iirAR = (iirAR * (1.0 - lowpass)) + (inputSampleR * lowpass);
        inputSampleR = iirAR;

        cycle++;
        if (cycle == cycleEnd)
        {
          // Per-tap feedback smoother. Update feedback first, then
          // mirror into previous tap. Reversing breaks the smoother.
          feedbackAL = (float)((double)feedbackAL * (1.0 - interpolate) + (double)previousAL * interpolate);
          previousAL = feedbackAL;
          feedbackBL = (float)((double)feedbackBL * (1.0 - interpolate) + (double)previousBL * interpolate);
          previousBL = feedbackBL;
          feedbackCL = (float)((double)feedbackCL * (1.0 - interpolate) + (double)previousCL * interpolate);
          previousCL = feedbackCL;
          feedbackDL = (float)((double)feedbackDL * (1.0 - interpolate) + (double)previousDL * interpolate);
          previousDL = feedbackDL;
          feedbackAR = (float)((double)feedbackAR * (1.0 - interpolate) + (double)previousAR * interpolate);
          previousAR = feedbackAR;
          feedbackBR = (float)((double)feedbackBR * (1.0 - interpolate) + (double)previousBR * interpolate);
          previousBR = feedbackBR;
          feedbackCR = (float)((double)feedbackCR * (1.0 - interpolate) + (double)previousCR * interpolate);
          previousCR = feedbackCR;
          feedbackDR = (float)((double)feedbackDR * (1.0 - interpolate) + (double)previousDR * interpolate);
          previousDR = feedbackDR;

          thunderL = (thunderL * 0.99) - ((double)feedbackAL * thunderAmount);
          thunderR = (thunderR * 0.99) - ((double)feedbackAR * thunderAmount);

          aIL[countI] = (float)(inputSampleL + (((double)feedbackAL + thunderL) * regen));
          aJL[countJ] = (float)(inputSampleL + ((double)feedbackBL * regen));
          aKL[countK] = (float)(inputSampleL + ((double)feedbackCL * regen));
          aLL[countL] = (float)(inputSampleL + ((double)feedbackDL * regen));
          aIR[countI] = (float)(inputSampleR + (((double)feedbackAR + thunderR) * regen));
          aJR[countJ] = (float)(inputSampleR + ((double)feedbackBR * regen));
          aKR[countK] = (float)(inputSampleR + ((double)feedbackCR * regen));
          aLR[countL] = (float)(inputSampleR + ((double)feedbackDR * regen));

          countI++; if (countI < 0 || countI > delayI) countI = 0;
          countJ++; if (countJ < 0 || countJ > delayJ) countJ = 0;
          countK++; if (countK < 0 || countK > delayK) countK = 0;
          countL++; if (countL < 0 || countL > delayL) countL = 0;

          double outIL = (double)aIL[countI - ((countI > delayI) ? delayI + 1 : 0)];
          double outJL = (double)aJL[countJ - ((countJ > delayJ) ? delayJ + 1 : 0)];
          double outKL = (double)aKL[countK - ((countK > delayK) ? delayK + 1 : 0)];
          double outLL = (double)aLL[countL - ((countL > delayL) ? delayL + 1 : 0)];
          double outIR = (double)aIR[countI - ((countI > delayI) ? delayI + 1 : 0)];
          double outJR = (double)aJR[countJ - ((countJ > delayJ) ? delayJ + 1 : 0)];
          double outKR = (double)aKR[countK - ((countK > delayK) ? delayK + 1 : 0)];
          double outLR = (double)aLR[countL - ((countL > delayL) ? delayL + 1 : 0)];

          aAL[countA] = (float)(outIL - (outJL + outKL + outLL));
          aBL[countB] = (float)(outJL - (outIL + outKL + outLL));
          aCL[countC] = (float)(outKL - (outIL + outJL + outLL));
          aDL[countD] = (float)(outLL - (outIL + outJL + outKL));
          aAR[countA] = (float)(outIR - (outJR + outKR + outLR));
          aBR[countB] = (float)(outJR - (outIR + outKR + outLR));
          aCR[countC] = (float)(outKR - (outIR + outJR + outLR));
          aDR[countD] = (float)(outLR - (outIR + outJR + outKR));

          countA++; if (countA < 0 || countA > delayA) countA = 0;
          countB++; if (countB < 0 || countB > delayB) countB = 0;
          countC++; if (countC < 0 || countC > delayC) countC = 0;
          countD++; if (countD < 0 || countD > delayD) countD = 0;

          double outAL = (double)aAL[countA - ((countA > delayA) ? delayA + 1 : 0)];
          double outBL = (double)aBL[countB - ((countB > delayB) ? delayB + 1 : 0)];
          double outCL = (double)aCL[countC - ((countC > delayC) ? delayC + 1 : 0)];
          double outDL = (double)aDL[countD - ((countD > delayD) ? delayD + 1 : 0)];
          double outAR = (double)aAR[countA - ((countA > delayA) ? delayA + 1 : 0)];
          double outBR = (double)aBR[countB - ((countB > delayB) ? delayB + 1 : 0)];
          double outCR = (double)aCR[countC - ((countC > delayC) ? delayC + 1 : 0)];
          double outDR = (double)aDR[countD - ((countD > delayD) ? delayD + 1 : 0)];

          aEL[countE] = (float)(outAL - (outBL + outCL + outDL));
          aFL[countF] = (float)(outBL - (outAL + outCL + outDL));
          aGL[countG] = (float)(outCL - (outAL + outBL + outDL));
          aHL[countH] = (float)(outDL - (outAL + outBL + outCL));
          aER[countE] = (float)(outAR - (outBR + outCR + outDR));
          aFR[countF] = (float)(outBR - (outAR + outCR + outDR));
          aGR[countG] = (float)(outCR - (outAR + outBR + outDR));
          aHR[countH] = (float)(outDR - (outAR + outBR + outCR));

          countE++; if (countE < 0 || countE > delayE) countE = 0;
          countF++; if (countF < 0 || countF > delayF) countF = 0;
          countG++; if (countG < 0 || countG > delayG) countG = 0;
          countH++; if (countH < 0 || countH > delayH) countH = 0;

          double outEL = (double)aEL[countE - ((countE > delayE) ? delayE + 1 : 0)];
          double outFL = (double)aFL[countF - ((countF > delayF) ? delayF + 1 : 0)];
          double outGL = (double)aGL[countG - ((countG > delayG) ? delayG + 1 : 0)];
          double outHL = (double)aHL[countH - ((countH > delayH) ? delayH + 1 : 0)];
          double outER = (double)aER[countE - ((countE > delayE) ? delayE + 1 : 0)];
          double outFR = (double)aFR[countF - ((countF > delayF) ? delayF + 1 : 0)];
          double outGR = (double)aGR[countG - ((countG > delayG) ? delayG + 1 : 0)];
          double outHR = (double)aHR[countH - ((countH > delayH) ? delayH + 1 : 0)];

          feedbackAL = (float)(outEL - (outFL + outGL + outHL));
          feedbackBL = (float)(outFL - (outEL + outGL + outHL));
          feedbackCL = (float)(outGL - (outEL + outFL + outHL));
          feedbackDL = (float)(outHL - (outEL + outFL + outGL));
          feedbackAR = (float)(outER - (outFR + outGR + outHR));
          feedbackBR = (float)(outFR - (outER + outGR + outHR));
          feedbackCR = (float)(outGR - (outER + outFR + outHR));
          feedbackDR = (float)(outHR - (outER + outFR + outGR));

          // Sum/8 combiner per AW source (NOT sum/4 -- intentional
          // gain reduction).
          inputSampleL = (outEL + outFL + outGL + outHL) * 0.125;
          inputSampleR = (outER + outFR + outGR + outHR) * 0.125;

          if (cycleEnd == 4)
          {
            lastRefL[0] = lastRefL[4];
            lastRefL[2] = (float)((lastRefL[0] + inputSampleL) * 0.5);
            lastRefL[1] = (float)((lastRefL[0] + lastRefL[2]) * 0.5);
            lastRefL[3] = (float)((lastRefL[2] + inputSampleL) * 0.5);
            lastRefL[4] = (float)inputSampleL;
            lastRefR[0] = lastRefR[4];
            lastRefR[2] = (float)((lastRefR[0] + inputSampleR) * 0.5);
            lastRefR[1] = (float)((lastRefR[0] + lastRefR[2]) * 0.5);
            lastRefR[3] = (float)((lastRefR[2] + inputSampleR) * 0.5);
            lastRefR[4] = (float)inputSampleR;
          }
          if (cycleEnd == 3)
          {
            lastRefL[0] = lastRefL[3];
            lastRefL[2] = (float)((lastRefL[0] + lastRefL[0] + inputSampleL) / 3.0);
            lastRefL[1] = (float)((lastRefL[0] + inputSampleL + inputSampleL) / 3.0);
            lastRefL[3] = (float)inputSampleL;
            lastRefR[0] = lastRefR[3];
            lastRefR[2] = (float)((lastRefR[0] + lastRefR[0] + inputSampleR) / 3.0);
            lastRefR[1] = (float)((lastRefR[0] + inputSampleR + inputSampleR) / 3.0);
            lastRefR[3] = (float)inputSampleR;
          }
          if (cycleEnd == 2)
          {
            lastRefL[0] = lastRefL[2];
            lastRefL[1] = (float)((lastRefL[0] + inputSampleL) * 0.5);
            lastRefL[2] = (float)inputSampleL;
            lastRefR[0] = lastRefR[2];
            lastRefR[1] = (float)((lastRefR[0] + inputSampleR) * 0.5);
            lastRefR[2] = (float)inputSampleR;
          }
          if (cycleEnd == 1)
          {
            lastRefL[0] = (float)inputSampleL;
            lastRefR[0] = (float)inputSampleR;
          }
          cycle = 0;
          inputSampleL = (double)lastRefL[cycle];
          inputSampleR = (double)lastRefR[cycle];
        }
        else
        {
          inputSampleL = (double)lastRefL[cycle];
          inputSampleR = (double)lastRefR[cycle];
        }

        if (fabs(iirBL) < 1.18e-37) iirBL = 0.0;
        iirBL = (iirBL * (1.0 - lowpass)) + (inputSampleL * lowpass);
        inputSampleL = iirBL;
        if (fabs(iirBR) < 1.18e-37) iirBR = 0.0;
        iirBR = (iirBR * (1.0 - lowpass)) + (inputSampleR * lowpass);
        inputSampleR = iirBR;

        // Submix wet/dry: 0.5 = full wet AND full dry summed.
        if (wet < 1.0) { inputSampleL *= wet; inputSampleR *= wet; }
        if (dry < 1.0) { drySampleL *= dry; drySampleR *= dry; }
        inputSampleL += drySampleL;
        inputSampleR += drySampleR;

        *out1 = (float)inputSampleL;
        *out2 = (float)inputSampleR;
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    float aIL[kVbI]; float aIR[kVbI];
    float aJL[kVbJ]; float aJR[kVbJ];
    float aKL[kVbK]; float aKR[kVbK];
    float aLL[kVbL]; float aLR[kVbL];
    float aAL[kVbA]; float aAR[kVbA];
    float aBL[kVbB]; float aBR[kVbB];
    float aCL[kVbC]; float aCR[kVbC];
    float aDL[kVbD]; float aDR[kVbD];
    float aEL[kVbE]; float aER[kVbE];
    float aFL[kVbF]; float aFR[kVbF];
    float aGL[kVbG]; float aGR[kVbG];
    float aHL[kVbH]; float aHR[kVbH];

    double iirAL, iirAR, iirBL, iirBR;
    float feedbackAL, feedbackAR, feedbackBL, feedbackBR;
    float feedbackCL, feedbackCR, feedbackDL, feedbackDR;
    float previousAL, previousAR, previousBL, previousBR;
    float previousCL, previousCR, previousDL, previousDR;
    double thunderL, thunderR;
    float lastRefL[7], lastRefR[7];

    int countA, countB, countC, countD;
    int countE, countF, countG, countH;
    int countI, countJ, countK, countL;
    int delayA, delayB, delayC, delayD;
    int delayE, delayF, delayG, delayH;
    int delayI, delayJ, delayK, delayL;
    int cycle;
#endif
  };

} // namespace house
