// house::Galactic
//
// Airwindows Galactic (Chris Johnson, MIT) port. The lush option:
// 3-stage cascaded 4x4 diff-Householder FDN + tiny 256-sample
// modulated predelay (LFO-driven vibrato) + full L<->R cross-
// coupling at the feedback stage. Sixth AW atom in the house
// package. Heaviest atom in the package due to 2 sin() per sample
// per channel for the LFO modulator.
//
// PHASE 1 HYBRID FLOAT (memory-necessary at this size):
//   - 12 FDN delay arrays: `float` (~503 KB stereo at float;
//     ~1 MB at double would blow L2 4x)
//   - Predelay aML/aMR: `float`
//   - feedback{A..D}{L,R}: `float` (small magnitudes)
//   - lastRef{L,R}[7]: `float`
//   - iir{A,B}{L,R}: `double` (per-sample IIR feedback path)
//   - vibM, oldfpd: `double` (LFO state -- drift is tiny, accumulates
//     across many samples, precision matters for stable vibrato)
//   - Block-rate scalars (regen, attenuate, lowpass, drift, size,
//     wet): `double`
//
// LOAD-BEARING from AW source (preserved literally per
// feedback_identical_means_identical):
//   - All count* init to 1, including countM; cycle init to 0
//   - **vibM init to 3.0** -- not 0; sets initial LFO phase
//   - **oldfpd init to 429496.7295** -- HUGE initial value;
//     produces an immediate first-frame vibM wrap that resets
//     oldfpd to ~0.43 for normal operation. AW intentional first-
//     frame settle behavior.
//   - LFO reset path: oldfpd = 0.4294967295 (we drop the
//     `+ fpdL * 0.0000000000618` random component since we have no
//     fpd RNG; the term contributes at most ~0.265 of noise, so
//     dropping it yields a deterministic-but-essentially-same LFO
//     seed)
//   - Attenuate gain compensation BEFORE predelay write:
//     attenuate = (1 - regen/0.125) * 1.333
//   - **Full L<->R cross-coupling** at FDN input lines I/J/K/L:
//     L gets R's feedback, R gets L's feedback. Different from
//     every other house atom. Signature lush stereo wash.
//   - Sum/8 combiner (not sum/4) at FDN output, same as Verbity
//   - Standard crossfade wet/dry (NOT submix; differs from Verbity
//     + CreamCoat)
//
// Vestigial source fields NOT ported (declared in AW header but
// never referenced in proc): vibML, vibMR, depthM, thunderL,
// thunderR.
//
// Dropped per template:
//   - Per-sample 32-bit dither (frexpf + pow per sample)
//   - rand()-seeded fpdL/fpdR
//   - VST host deps
//
// CPU note: 2 sin() per sample per channel = 4 sin/sample total.
// Cortex-A8 scalar sin is libm (~50ns each) -- ~10% CPU floor for
// LFO alone at 48k stereo. Projected total ~25-30% stereo,
// heaviest house atom. Defer Phase 2 sin-polynomial optimization
// until CPU bites.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace house
{

  // Delay-line sizes (verbatim from AW Galactic.h). Same 12 FDN
  // line sizes as Verbity; only the predelay differs.
  static const int kGxI = 6480;
  static const int kGxJ = 3660;
  static const int kGxK = 1720;
  static const int kGxL = 680;
  static const int kGxA = 9700;
  static const int kGxB = 6000;
  static const int kGxC = 2320;
  static const int kGxD = 940;
  static const int kGxE = 15220;
  static const int kGxF = 8460;
  static const int kGxG = 4540;
  static const int kGxH = 3200;
  static const int kGxM = 3111; // predelay buffer (delayM = 256 in use)

  class Galactic : public od::Object
  {
  public:
    Galactic()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mReplace);
      addParameter(mBrightness);
      addParameter(mDetune);
      addParameter(mBigDim);
      addParameter(mDryWet);

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
      memset(aML, 0, sizeof(aML)); memset(aMR, 0, sizeof(aMR));
      memset(lastRefL, 0, sizeof(lastRefL));
      memset(lastRefR, 0, sizeof(lastRefR));

      iirAL = iirAR = 0.0;
      iirBL = iirBR = 0.0;
      feedbackAL = feedbackAR = 0.0f;
      feedbackBL = feedbackBR = 0.0f;
      feedbackCL = feedbackCR = 0.0f;
      feedbackDL = feedbackDR = 0.0f;

      countA = countB = countC = countD = 1;
      countE = countF = countG = countH = 1;
      countI = countJ = countK = countL = 1;
      countM = 1;
      cycle = 0;

      // LOAD-BEARING from AW source.
      vibM = 3.0;
      oldfpd = 429496.7295;
    }

    virtual ~Galactic() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mReplace{"Replace", 0.5f};
    od::Parameter mBrightness{"Brightness", 0.5f};
    od::Parameter mDetune{"Detune", 0.5f};
    od::Parameter mBigDim{"BigDim", 1.0f};
    od::Parameter mDryWet{"DryWet", 1.0f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      const float A = mReplace.value();
      const float B = mBrightness.value();
      const float C = mDetune.value();
      const float D = mBigDim.value();
      const float E = mDryWet.value();

      double overallscale = 1.0;
      overallscale /= 44100.0;
      overallscale *= (double)globalConfig.sampleRate;

      int cycleEnd = (int)floor(overallscale);
      if (cycleEnd < 1) cycleEnd = 1;
      if (cycleEnd > 4) cycleEnd = 4;
      if (cycle > cycleEnd - 1) cycle = cycleEnd - 1;

      double regen = 0.0625 + ((1.0 - (double)A) * 0.0625);
      double attenuate = (1.0 - (regen / 0.125)) * 1.333;
      double lowpass = pow(1.00001 - (1.0 - (double)B), 2.0) / sqrt(overallscale);
      double drift = pow((double)C, 3.0) * 0.001;
      double size = ((double)D * 1.77) + 0.1;
      double wet = 1.0 - pow(1.0 - (double)E, 3.0);

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
      delayM = 256;

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        double inputSampleL = *in1;
        double inputSampleR = *in2;
        if (fabs(inputSampleL) < 1.18e-23) inputSampleL = 1.18e-17;
        if (fabs(inputSampleR) < 1.18e-23) inputSampleR = 1.18e-17;
        double drySampleL = inputSampleL;
        double drySampleR = inputSampleR;

        // LFO accumulate + wrap. On wrap, re-seed oldfpd to settled
        // value (~0.43); first wrap converts the 429496.7295 init
        // burst into normal operation.
        vibM += (oldfpd * drift);
        if (vibM > (3.141592653589793238 * 2.0))
        {
          vibM = 0.0;
          oldfpd = 0.4294967295;
        }

        aML[countM] = (float)(inputSampleL * attenuate);
        aMR[countM] = (float)(inputSampleR * attenuate);
        countM++; if (countM < 0 || countM > delayM) countM = 0;

        double offsetML = (sin(vibM) + 1.0) * 127.0;
        double offsetMR = (sin(vibM + (3.141592653589793238 / 2.0)) + 1.0) * 127.0;
        int workingML = countM + (int)offsetML;
        int workingMR = countM + (int)offsetMR;
        double fracML = offsetML - floor(offsetML);
        double fracMR = offsetMR - floor(offsetMR);
        double interpolML = ((double)aML[workingML - ((workingML > delayM) ? delayM + 1 : 0)] * (1.0 - fracML));
        interpolML += ((double)aML[workingML + 1 - ((workingML + 1 > delayM) ? delayM + 1 : 0)] * fracML);
        double interpolMR = ((double)aMR[workingMR - ((workingMR > delayM) ? delayM + 1 : 0)] * (1.0 - fracMR));
        interpolMR += ((double)aMR[workingMR + 1 - ((workingMR + 1 > delayM) ? delayM + 1 : 0)] * fracMR);
        inputSampleL = interpolML;
        inputSampleR = interpolMR;

        iirAL = (iirAL * (1.0 - lowpass)) + (inputSampleL * lowpass);
        inputSampleL = iirAL;
        iirAR = (iirAR * (1.0 - lowpass)) + (inputSampleR * lowpass);
        inputSampleR = iirAR;

        cycle++;
        if (cycle == cycleEnd)
        {
          // CROSS-COUPLED feedback: L input gets R's feedback,
          // R input gets L's. Signature Galactic stereo wash.
          aIL[countI] = (float)(inputSampleL + ((double)feedbackAR * regen));
          aJL[countJ] = (float)(inputSampleL + ((double)feedbackBR * regen));
          aKL[countK] = (float)(inputSampleL + ((double)feedbackCR * regen));
          aLL[countL] = (float)(inputSampleL + ((double)feedbackDR * regen));
          aIR[countI] = (float)(inputSampleR + ((double)feedbackAL * regen));
          aJR[countJ] = (float)(inputSampleR + ((double)feedbackBL * regen));
          aKR[countK] = (float)(inputSampleR + ((double)feedbackCL * regen));
          aLR[countL] = (float)(inputSampleR + ((double)feedbackDL * regen));

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

        iirBL = (iirBL * (1.0 - lowpass)) + (inputSampleL * lowpass);
        inputSampleL = iirBL;
        iirBR = (iirBR * (1.0 - lowpass)) + (inputSampleR * lowpass);
        inputSampleR = iirBR;

        // Standard crossfade wet/dry (NOT submix).
        if (wet < 1.0)
        {
          inputSampleL = (inputSampleL * wet) + (drySampleL * (1.0 - wet));
          inputSampleR = (inputSampleR * wet) + (drySampleR * (1.0 - wet));
        }

        *out1 = (float)inputSampleL;
        *out2 = (float)inputSampleR;
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    float aIL[kGxI]; float aIR[kGxI];
    float aJL[kGxJ]; float aJR[kGxJ];
    float aKL[kGxK]; float aKR[kGxK];
    float aLL[kGxL]; float aLR[kGxL];
    float aAL[kGxA]; float aAR[kGxA];
    float aBL[kGxB]; float aBR[kGxB];
    float aCL[kGxC]; float aCR[kGxC];
    float aDL[kGxD]; float aDR[kGxD];
    float aEL[kGxE]; float aER[kGxE];
    float aFL[kGxF]; float aFR[kGxF];
    float aGL[kGxG]; float aGR[kGxG];
    float aHL[kGxH]; float aHR[kGxH];
    float aML[kGxM]; float aMR[kGxM];

    double iirAL, iirAR, iirBL, iirBR;
    float feedbackAL, feedbackAR, feedbackBL, feedbackBR;
    float feedbackCL, feedbackCR, feedbackDL, feedbackDR;
    float lastRefL[7], lastRefR[7];
    double vibM, oldfpd;

    int countA, countB, countC, countD;
    int countE, countF, countG, countH;
    int countI, countJ, countK, countL;
    int countM;
    int delayA, delayB, delayC, delayD;
    int delayE, delayF, delayG, delayH;
    int delayI, delayJ, delayK, delayL;
    int delayM;
    int cycle;
#endif
  };

} // namespace house
