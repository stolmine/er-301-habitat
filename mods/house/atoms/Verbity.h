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
      // float bakes for the pure-float per-sample FDN (removes the (double) cast-traps)
      float interpolateF=(float)interpolate, regenF=(float)regen, thunderAmountF=(float)thunderAmount;
      float lowpassF=(float)lowpass, wetF=(float)wet, dryF=(float)dry;

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
        float inputSampleL = *in1;
        float inputSampleR = *in2;
        if (fabsf(inputSampleL) < 1.18e-23f) inputSampleL = 1.18e-17f;
        if (fabsf(inputSampleR) < 1.18e-23f) inputSampleR = 1.18e-17f;
        float drySampleL = inputSampleL;
        float drySampleR = inputSampleR;

        if (fabsf(iirAL) < 1.18e-37f) iirAL = 0.0f;
        iirAL = (iirAL * (1.0f - lowpassF)) + (inputSampleL * lowpassF);
        inputSampleL = iirAL;
        if (fabsf(iirAR) < 1.18e-37f) iirAR = 0.0f;
        iirAR = (iirAR * (1.0f - lowpassF)) + (inputSampleR * lowpassF);
        inputSampleR = iirAR;

        cycle++;
        if (cycle == cycleEnd)
        {
          // Per-tap feedback smoother. Update feedback first, then
          // mirror into previous tap. Reversing breaks the smoother.
          feedbackAL = (feedbackAL * (1.0f - interpolateF) + previousAL * interpolateF);
          previousAL = feedbackAL;
          feedbackBL = (feedbackBL * (1.0f - interpolateF) + previousBL * interpolateF);
          previousBL = feedbackBL;
          feedbackCL = (feedbackCL * (1.0f - interpolateF) + previousCL * interpolateF);
          previousCL = feedbackCL;
          feedbackDL = (feedbackDL * (1.0f - interpolateF) + previousDL * interpolateF);
          previousDL = feedbackDL;
          feedbackAR = (feedbackAR * (1.0f - interpolateF) + previousAR * interpolateF);
          previousAR = feedbackAR;
          feedbackBR = (feedbackBR * (1.0f - interpolateF) + previousBR * interpolateF);
          previousBR = feedbackBR;
          feedbackCR = (feedbackCR * (1.0f - interpolateF) + previousCR * interpolateF);
          previousCR = feedbackCR;
          feedbackDR = (feedbackDR * (1.0f - interpolateF) + previousDR * interpolateF);
          previousDR = feedbackDR;

          thunderL = (thunderL * 0.99f) - (feedbackAL * thunderAmountF);
          thunderR = (thunderR * 0.99f) - (feedbackAR * thunderAmountF);

          aIL[countI] = (inputSampleL + ((feedbackAL + thunderL) * regenF));
          aJL[countJ] = (inputSampleL + (feedbackBL * regenF));
          aKL[countK] = (inputSampleL + (feedbackCL * regenF));
          aLL[countL] = (inputSampleL + (feedbackDL * regenF));
          aIR[countI] = (inputSampleR + ((feedbackAR + thunderR) * regenF));
          aJR[countJ] = (inputSampleR + (feedbackBR * regenF));
          aKR[countK] = (inputSampleR + (feedbackCR * regenF));
          aLR[countL] = (inputSampleR + (feedbackDR * regenF));

          countI++; if (countI < 0 || countI > delayI) countI = 0;
          countJ++; if (countJ < 0 || countJ > delayJ) countJ = 0;
          countK++; if (countK < 0 || countK > delayK) countK = 0;
          countL++; if (countL < 0 || countL > delayL) countL = 0;

          float outIL = aIL[countI - ((countI > delayI) ? delayI + 1 : 0)];
          float outJL = aJL[countJ - ((countJ > delayJ) ? delayJ + 1 : 0)];
          float outKL = aKL[countK - ((countK > delayK) ? delayK + 1 : 0)];
          float outLL = aLL[countL - ((countL > delayL) ? delayL + 1 : 0)];
          float outIR = aIR[countI - ((countI > delayI) ? delayI + 1 : 0)];
          float outJR = aJR[countJ - ((countJ > delayJ) ? delayJ + 1 : 0)];
          float outKR = aKR[countK - ((countK > delayK) ? delayK + 1 : 0)];
          float outLR = aLR[countL - ((countL > delayL) ? delayL + 1 : 0)];

          aAL[countA] = (outIL - (outJL + outKL + outLL));
          aBL[countB] = (outJL - (outIL + outKL + outLL));
          aCL[countC] = (outKL - (outIL + outJL + outLL));
          aDL[countD] = (outLL - (outIL + outJL + outKL));
          aAR[countA] = (outIR - (outJR + outKR + outLR));
          aBR[countB] = (outJR - (outIR + outKR + outLR));
          aCR[countC] = (outKR - (outIR + outJR + outLR));
          aDR[countD] = (outLR - (outIR + outJR + outKR));

          countA++; if (countA < 0 || countA > delayA) countA = 0;
          countB++; if (countB < 0 || countB > delayB) countB = 0;
          countC++; if (countC < 0 || countC > delayC) countC = 0;
          countD++; if (countD < 0 || countD > delayD) countD = 0;

          float outAL = aAL[countA - ((countA > delayA) ? delayA + 1 : 0)];
          float outBL = aBL[countB - ((countB > delayB) ? delayB + 1 : 0)];
          float outCL = aCL[countC - ((countC > delayC) ? delayC + 1 : 0)];
          float outDL = aDL[countD - ((countD > delayD) ? delayD + 1 : 0)];
          float outAR = aAR[countA - ((countA > delayA) ? delayA + 1 : 0)];
          float outBR = aBR[countB - ((countB > delayB) ? delayB + 1 : 0)];
          float outCR = aCR[countC - ((countC > delayC) ? delayC + 1 : 0)];
          float outDR = aDR[countD - ((countD > delayD) ? delayD + 1 : 0)];

          aEL[countE] = (outAL - (outBL + outCL + outDL));
          aFL[countF] = (outBL - (outAL + outCL + outDL));
          aGL[countG] = (outCL - (outAL + outBL + outDL));
          aHL[countH] = (outDL - (outAL + outBL + outCL));
          aER[countE] = (outAR - (outBR + outCR + outDR));
          aFR[countF] = (outBR - (outAR + outCR + outDR));
          aGR[countG] = (outCR - (outAR + outBR + outDR));
          aHR[countH] = (outDR - (outAR + outBR + outCR));

          countE++; if (countE < 0 || countE > delayE) countE = 0;
          countF++; if (countF < 0 || countF > delayF) countF = 0;
          countG++; if (countG < 0 || countG > delayG) countG = 0;
          countH++; if (countH < 0 || countH > delayH) countH = 0;

          float outEL = aEL[countE - ((countE > delayE) ? delayE + 1 : 0)];
          float outFL = aFL[countF - ((countF > delayF) ? delayF + 1 : 0)];
          float outGL = aGL[countG - ((countG > delayG) ? delayG + 1 : 0)];
          float outHL = aHL[countH - ((countH > delayH) ? delayH + 1 : 0)];
          float outER = aER[countE - ((countE > delayE) ? delayE + 1 : 0)];
          float outFR = aFR[countF - ((countF > delayF) ? delayF + 1 : 0)];
          float outGR = aGR[countG - ((countG > delayG) ? delayG + 1 : 0)];
          float outHR = aHR[countH - ((countH > delayH) ? delayH + 1 : 0)];

          feedbackAL = (outEL - (outFL + outGL + outHL));
          feedbackBL = (outFL - (outEL + outGL + outHL));
          feedbackCL = (outGL - (outEL + outFL + outHL));
          feedbackDL = (outHL - (outEL + outFL + outGL));
          feedbackAR = (outER - (outFR + outGR + outHR));
          feedbackBR = (outFR - (outER + outGR + outHR));
          feedbackCR = (outGR - (outER + outFR + outHR));
          feedbackDR = (outHR - (outER + outFR + outGR));

          // Sum/8 combiner per AW source (NOT sum/4 -- intentional
          // gain reduction).
          inputSampleL = (outEL + outFL + outGL + outHL) * 0.125f;
          inputSampleR = (outER + outFR + outGR + outHR) * 0.125f;

          if (cycleEnd == 4)
          {
            lastRefL[0] = lastRefL[4];
            lastRefL[2] = ((lastRefL[0] + inputSampleL) * 0.5f);
            lastRefL[1] = ((lastRefL[0] + lastRefL[2]) * 0.5f);
            lastRefL[3] = ((lastRefL[2] + inputSampleL) * 0.5f);
            lastRefL[4] = inputSampleL;
            lastRefR[0] = lastRefR[4];
            lastRefR[2] = ((lastRefR[0] + inputSampleR) * 0.5f);
            lastRefR[1] = ((lastRefR[0] + lastRefR[2]) * 0.5f);
            lastRefR[3] = ((lastRefR[2] + inputSampleR) * 0.5f);
            lastRefR[4] = inputSampleR;
          }
          if (cycleEnd == 3)
          {
            lastRefL[0] = lastRefL[3];
            lastRefL[2] = ((lastRefL[0] + lastRefL[0] + inputSampleL) / 3.0f);
            lastRefL[1] = ((lastRefL[0] + inputSampleL + inputSampleL) / 3.0f);
            lastRefL[3] = inputSampleL;
            lastRefR[0] = lastRefR[3];
            lastRefR[2] = ((lastRefR[0] + lastRefR[0] + inputSampleR) / 3.0f);
            lastRefR[1] = ((lastRefR[0] + inputSampleR + inputSampleR) / 3.0f);
            lastRefR[3] = inputSampleR;
          }
          if (cycleEnd == 2)
          {
            lastRefL[0] = lastRefL[2];
            lastRefL[1] = ((lastRefL[0] + inputSampleL) * 0.5f);
            lastRefL[2] = inputSampleL;
            lastRefR[0] = lastRefR[2];
            lastRefR[1] = ((lastRefR[0] + inputSampleR) * 0.5f);
            lastRefR[2] = inputSampleR;
          }
          if (cycleEnd == 1)
          {
            lastRefL[0] = inputSampleL;
            lastRefR[0] = inputSampleR;
          }
          cycle = 0;
          inputSampleL = lastRefL[cycle];
          inputSampleR = lastRefR[cycle];
        }
        else
        {
          inputSampleL = lastRefL[cycle];
          inputSampleR = lastRefR[cycle];
        }

        if (fabs(iirBL) < 1.18e-37) iirBL = 0.0;
        iirBL = (iirBL * (1.0 - lowpassF)) + (inputSampleL * lowpassF);
        inputSampleL = iirBL;
        if (fabs(iirBR) < 1.18e-37) iirBR = 0.0;
        iirBR = (iirBR * (1.0 - lowpassF)) + (inputSampleR * lowpassF);
        inputSampleR = iirBR;

        // Submix wet/dry: 0.5 = full wet AND full dry summed.
        if (wet < 1.0) { inputSampleL *= wet; inputSampleR *= wet; }
        if (dry < 1.0) { drySampleL *= dry; drySampleR *= dry; }
        inputSampleL += drySampleL;
        inputSampleR += drySampleR;

        *out1 = inputSampleL;
        *out2 = inputSampleR;
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

    float iirAL, iirAR, iirBL, iirBR;
    float feedbackAL, feedbackAR, feedbackBL, feedbackBR;
    float feedbackCL, feedbackCR, feedbackDL, feedbackDR;
    float previousAL, previousAR, previousBL, previousBR;
    float previousCL, previousCR, previousDL, previousDR;
    float thunderL, thunderR;
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
