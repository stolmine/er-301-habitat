// house::CreamCoat
//
// Airwindows CreamCoat (Chris Johnson, MIT) port. Bright-ambience
// engine with the canonical divisor+Bezier mechanic exposed as a
// user knob (DeRez). Third AW atom in the house package after
// kWoodRoom and WoodenBox.
//
// PHASE 1 HYBRID FLOAT CONVERSION (deviation from prior atoms):
// kWoodRoom and WoodenBox both ship as full double per the
// template's Phase 1 default. CreamCoat is significantly heavier
// (~568 KB stereo at double vs WoodenBox's 53 KB) -- predelay
// buffer alone is 240 KB stereo. To avoid ping-ponging the port
// and to fit comfortably in the Cortex-A8 256 KB L2, we apply
// the float-conversion hybrid up-front:
//
//   - State arrays (FDN lines + predelay): `float` (~284 KB saved)
//   - Per-sample intermediates / Householder math: `float`
//   - Feedback taps: `float` (small magnitudes, safe)
//   - Bezier accumulators (bez[]): KEEP `double` -- precision
//     critical at low DeRez where many samples sum into
//     bez_SampL/R before the cycle fires
//   - Block-rate scalars (overallscale, derez, regen, wet, dry):
//     KEEP `double` -- 1/derez quantization math benefits from
//     precision, costs nothing to keep
//
// Per the handoff (planning/refs/airwindows-port-handoff.md §4.3
// step 1): "Step zero for any AW port is converting the algorithm
// to float and running NEON." kWoodRoom/WoodenBox skipped step
// zero conservatively; for CreamCoat the size makes it worth
// doing up front. Also positions for the eventual NEON
// Householder reduction (Phase 3+) which requires float quads.
//
// Risk: feedback-loop drift / denormal accumulation. Mitigations:
// Householder math is sum-preserving (no gain explosion);
// denormal flush still in place at per-sample input boundary;
// bez[] accumulator (the most precision-sensitive path) stays
// double. If A/B against AW reference reveals audible
// regression, escalate more intermediates back to double.
//
// LOAD-BEARING from AW source (preserved literally per
// feedback_identical_means_identical):
//   - Counters init to 1 (read formula returns arr[0] safely)
//   - bez[cmco_cycle] is NOT initialized to 1.0 (unlike kWoodRoom
//     and WoodenBox). CreamCoat starts at 0; first cycle fires
//     after enough accumulation. At default DeRez=1.0 that's one
//     sample of silence (inaudible). At very low DeRez that's
//     several ms of silence on first insert. AW behavior; do not
//     "fix."
//   - Submix wet/dry: Wetness=0.5 outputs full wet AND full dry
//     summed (not crossfaded). For send patches.
//
// Dropped per template:
//   - Per-sample 32-bit dither (frexpf + pow per sample)
//   - rand()-seeded fpdL/fpdR
//   - VST host deps
// Dropped as unused in AW source:
//   - previous{A..E}{L,R} fields (declared in AW header,
//     initialized to 0 in ctor, never referenced in proc).
//     Vestigial from an earlier version.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace house
{

  // Constants verbatim from AW CreamCoat.h
  static const int kCreamCoatPredelay = 15000;

  static const int kshortA = 350;
  static const int kshortB = 1710;
  static const int kshortC = 1610;
  static const int kshortD = 835;
  static const int kshortE = 700;
  static const int kshortF = 1260;
  static const int kshortG = 1110;
  static const int kshortH = 1768;
  static const int kshortI = 280;
  static const int kshortJ = 2645;
  static const int kshortK = 1410;
  static const int kshortL = 1175;
  static const int kshortM = 12;
  static const int kshortN = 3110;
  static const int kshortO = 120;
  static const int kshortP = 2370;

  // bez[] slot indices (verbatim from AW CreamCoat.h enum).
  // CreamCoat has two extras vs WoodenBox: InL/InR + UnInL/UnInR
  // for the previous-sample / uninterpolated input tracking.
  enum CreamCoatBezSlot
  {
    cmco_AL = 0,
    cmco_AR,
    cmco_BL,
    cmco_BR,
    cmco_CL,
    cmco_CR,
    cmco_InL,
    cmco_InR,
    cmco_UnInL,
    cmco_UnInR,
    cmco_SampL,
    cmco_SampR,
    cmco_cycle,
    cmco_total
  };

  class CreamCoat : public od::Object
  {
  public:
    CreamCoat()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mSelect);
      addParameter(mRegen);
      addParameter(mDeRez);
      addParameter(mPredlay);
      addParameter(mWetness);

      // Zero all 16 FDN line arrays per side (float).
      memset(aAL, 0, sizeof(aAL)); memset(aAR, 0, sizeof(aAR));
      memset(aBL, 0, sizeof(aBL)); memset(aBR, 0, sizeof(aBR));
      memset(aCL, 0, sizeof(aCL)); memset(aCR, 0, sizeof(aCR));
      memset(aDL, 0, sizeof(aDL)); memset(aDR, 0, sizeof(aDR));
      memset(aEL, 0, sizeof(aEL)); memset(aER, 0, sizeof(aER));
      memset(aFL, 0, sizeof(aFL)); memset(aFR, 0, sizeof(aFR));
      memset(aGL, 0, sizeof(aGL)); memset(aGR, 0, sizeof(aGR));
      memset(aHL, 0, sizeof(aHL)); memset(aHR, 0, sizeof(aHR));
      memset(aIL, 0, sizeof(aIL)); memset(aIR, 0, sizeof(aIR));
      memset(aJL, 0, sizeof(aJL)); memset(aJR, 0, sizeof(aJR));
      memset(aKL, 0, sizeof(aKL)); memset(aKR, 0, sizeof(aKR));
      memset(aLL, 0, sizeof(aLL)); memset(aLR, 0, sizeof(aLR));
      memset(aML, 0, sizeof(aML)); memset(aMR, 0, sizeof(aMR));
      memset(aNL, 0, sizeof(aNL)); memset(aNR, 0, sizeof(aNR));
      memset(aOL, 0, sizeof(aOL)); memset(aOR, 0, sizeof(aOR));
      memset(aPL, 0, sizeof(aPL)); memset(aPR, 0, sizeof(aPR));

      // Predelay buffer (float).
      memset(aZL, 0, sizeof(aZL));
      memset(aZR, 0, sizeof(aZR));

      // Counters init to 1 per AW source.
      countAL = countBL = countCL = countDL = 1;
      countEL = countFL = countGL = countHL = 1;
      countIL = countJL = countKL = countLL = 1;
      countML = countNL = countOL = countPL = 1;
      countAR = countBR = countCR = countDR = 1;
      countER = countFR = countGR = countHR = 1;
      countIR = countJR = countKR = countLR = 1;
      countMR = countNR = countOR = countPR = 1;
      countZ = 0;

      feedbackAL = feedbackBL = feedbackCL = feedbackDL = 0.0f;
      feedbackDR = feedbackHR = feedbackLR = feedbackPR = 0.0f;

      for (int x = 0; x < cmco_total; x++) bez[x] = 0.0;
      // NOTE: bez[cmco_cycle] is intentionally NOT initialized to
      // 1.0 (unlike kWoodRoom and WoodenBox). This is AW
      // CreamCoat behavior — first cycle fires after enough
      // accumulation. At default DeRez=1.0, one sample silence;
      // at low DeRez, several ms silence.

      // Initial short* values = k* defaults; prevclearcoat = -1
      // forces a Select-table load on first process() call.
      shortA = kshortA; shortB = kshortB; shortC = kshortC; shortD = kshortD;
      shortE = kshortE; shortF = kshortF; shortG = kshortG; shortH = kshortH;
      shortI = kshortI; shortJ = kshortJ; shortK = kshortK; shortL = kshortL;
      shortM = kshortM; shortN = kshortN; shortO = kshortO; shortP = kshortP;
      prevclearcoat = -1;
    }

    virtual ~CreamCoat() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};

    od::Parameter mSelect{"Select", 0.5f};
    od::Parameter mRegen{"Regen", 0.5f};
    od::Parameter mDeRez{"DeRez", 1.0f};   // AW default
    od::Parameter mPredlay{"Predlay", 0.0f};
    od::Parameter mWetness{"Wetness", 0.25f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      const float A = mSelect.value();
      const float B = mRegen.value();
      const float C = mDeRez.value();
      const float D = mPredlay.value();
      const float E = mWetness.value();

      // Block-rate scalars stay double for derez quantization.
      double overallscale = 1.0;
      overallscale /= 44100.0;
      overallscale *= (double)globalConfig.sampleRate;

      int clearcoat = (int)(A * 16.999f);
      if (clearcoat < 0) clearcoat = 0;
      if (clearcoat > 16) clearcoat = 16;
      if (clearcoat != prevclearcoat)
      {
        // Heavy state reset on Select change: zero all 32 FDN
        // arrays + 32 counters reset + preset table load.
        memset(aAL, 0, sizeof(aAL)); memset(aAR, 0, sizeof(aAR));
        memset(aBL, 0, sizeof(aBL)); memset(aBR, 0, sizeof(aBR));
        memset(aCL, 0, sizeof(aCL)); memset(aCR, 0, sizeof(aCR));
        memset(aDL, 0, sizeof(aDL)); memset(aDR, 0, sizeof(aDR));
        memset(aEL, 0, sizeof(aEL)); memset(aER, 0, sizeof(aER));
        memset(aFL, 0, sizeof(aFL)); memset(aFR, 0, sizeof(aFR));
        memset(aGL, 0, sizeof(aGL)); memset(aGR, 0, sizeof(aGR));
        memset(aHL, 0, sizeof(aHL)); memset(aHR, 0, sizeof(aHR));
        memset(aIL, 0, sizeof(aIL)); memset(aIR, 0, sizeof(aIR));
        memset(aJL, 0, sizeof(aJL)); memset(aJR, 0, sizeof(aJR));
        memset(aKL, 0, sizeof(aKL)); memset(aKR, 0, sizeof(aKR));
        memset(aLL, 0, sizeof(aLL)); memset(aLR, 0, sizeof(aLR));
        memset(aML, 0, sizeof(aML)); memset(aMR, 0, sizeof(aMR));
        memset(aNL, 0, sizeof(aNL)); memset(aNR, 0, sizeof(aNR));
        memset(aOL, 0, sizeof(aOL)); memset(aOR, 0, sizeof(aOR));
        memset(aPL, 0, sizeof(aPL)); memset(aPR, 0, sizeof(aPR));

        countAL = countBL = countCL = countDL = 1;
        countEL = countFL = countGL = countHL = 1;
        countIL = countJL = countKL = countLL = 1;
        countML = countNL = countOL = countPL = 1;
        countAR = countBR = countCR = countDR = 1;
        countER = countFR = countGR = countHR = 1;
        countIR = countJR = countKR = countLR = 1;
        countMR = countNR = countOR = countPR = 1;

        // Preset tables verbatim from AW source switch.
        switch (clearcoat)
        {
        case 0:
          shortA = 65; shortB = 124; shortC = 83; shortD = 180; shortE = 200; shortF = 291; shortG = 108; shortH = 189; shortI = 73; shortJ = 410; shortK = 479; shortL = 310; shortM = 11; shortN = 928; shortO = 23; shortP = 654; break;
        case 1:
          shortA = 114; shortB = 205; shortC = 498; shortD = 195; shortE = 205; shortF = 318; shortG = 143; shortH = 254; shortI = 64; shortJ = 721; shortK = 512; shortL = 324; shortM = 11; shortN = 782; shortO = 26; shortP = 394; break;
        case 2:
          shortA = 118; shortB = 272; shortC = 292; shortD = 145; shortE = 200; shortF = 241; shortG = 204; shortH = 504; shortI = 50; shortJ = 678; shortK = 424; shortL = 412; shortM = 11; shortN = 1124; shortO = 47; shortP = 766; break;
        case 3:
          shortA = 19; shortB = 474; shortC = 301; shortD = 275; shortE = 260; shortF = 321; shortG = 371; shortH = 571; shortI = 50; shortJ = 410; shortK = 697; shortL = 414; shortM = 11; shortN = 986; shortO = 47; shortP = 522; break;
        case 4:
          shortA = 112; shortB = 387; shortC = 452; shortD = 289; shortE = 173; shortF = 476; shortG = 321; shortH = 593; shortI = 73; shortJ = 343; shortK = 829; shortL = 91; shortM = 11; shortN = 1055; shortO = 43; shortP = 862; break;
        case 5:
          shortA = 60; shortB = 368; shortC = 295; shortD = 272; shortE = 210; shortF = 284; shortG = 326; shortH = 830; shortI = 125; shortJ = 236; shortK = 737; shortL = 486; shortM = 11; shortN = 1178; shortO = 75; shortP = 902; break;
        case 6:
          shortA = 73; shortB = 311; shortC = 472; shortD = 251; shortE = 134; shortF = 509; shortG = 393; shortH = 591; shortI = 124; shortJ = 1070; shortK = 340; shortL = 525; shortM = 11; shortN = 1367; shortO = 75; shortP = 816; break;
        case 7:
          shortA = 159; shortB = 518; shortC = 514; shortD = 165; shortE = 275; shortF = 494; shortG = 296; shortH = 667; shortI = 75; shortJ = 1101; shortK = 116; shortL = 414; shortM = 11; shortN = 1261; shortO = 79; shortP = 998; break;
        case 8:
          shortA = 41; shortB = 741; shortC = 274; shortD = 59; shortE = 306; shortF = 332; shortG = 291; shortH = 767; shortI = 42; shortJ = 881; shortK = 959; shortL = 422; shortM = 11; shortN = 1237; shortO = 45; shortP = 958; break;
        case 9:
          shortA = 251; shortB = 437; shortC = 783; shortD = 189; shortE = 130; shortF = 272; shortG = 244; shortH = 761; shortI = 128; shortJ = 1190; shortK = 320; shortL = 491; shortM = 11; shortN = 1409; shortO = 58; shortP = 455; break;
        case 10:
          shortA = 316; shortB = 510; shortC = 1087; shortD = 349; shortE = 359; shortF = 74; shortG = 79; shortH = 1269; shortI = 34; shortJ = 693; shortK = 749; shortL = 511; shortM = 11; shortN = 1751; shortO = 93; shortP = 403; break;
        case 11:
          shortA = 254; shortB = 651; shortC = 845; shortD = 316; shortE = 373; shortF = 267; shortG = 182; shortH = 857; shortI = 215; shortJ = 1535; shortK = 1127; shortL = 315; shortM = 11; shortN = 1649; shortO = 97; shortP = 829; break;
        case 12:
          shortA = 113; shortB = 101; shortC = 673; shortD = 357; shortE = 340; shortF = 229; shortG = 278; shortH = 1008; shortI = 265; shortJ = 1890; shortK = 155; shortL = 267; shortM = 11; shortN = 2233; shortO = 116; shortP = 600; break;
        case 13:
          shortA = 218; shortB = 1058; shortC = 862; shortD = 505; shortE = 297; shortF = 580; shortG = 532; shortH = 1387; shortI = 120; shortJ = 576; shortK = 1409; shortL = 473; shortM = 11; shortN = 1991; shortO = 76; shortP = 685; break;
        case 14:
          shortA = 78; shortB = 760; shortC = 982; shortD = 528; shortE = 445; shortF = 1128; shortG = 130; shortH = 708; shortI = 22; shortJ = 2144; shortK = 354; shortL = 1169; shortM = 11; shortN = 2782; shortO = 58; shortP = 1515; break;
        case 15:
          shortA = 330; shortB = 107; shortC = 1110; shortD = 371; shortE = 620; shortF = 143; shortG = 1014; shortH = 1763; shortI = 184; shortJ = 2068; shortK = 1406; shortL = 595; shortM = 11; shortN = 2639; shortO = 33; shortP = 1594; break;
        case 16:
        default:
          shortA = 336; shortB = 1660; shortC = 386; shortD = 623; shortE = 693; shortF = 1079; shortG = 891; shortH = 1574; shortI = 24; shortJ = 2641; shortK = 1239; shortL = 775; shortM = 11; shortN = 3104; shortO = 55; shortP = 2366; break;
        }
        prevclearcoat = clearcoat;
      }

      // Block-rate scalars stay double.
      double regen = (1.0 - pow(1.0 - (double)B, 2.0)) * 0.0625;
      double derez = (double)C / overallscale;
      if (derez < 0.0005) derez = 0.0005;
      if (derez > 1.0) derez = 1.0;
      derez = 1.0 / ((int)(1.0 / derez));
      int adjPredelay = (int)((double)kCreamCoatPredelay * (double)D * derez);
      double wet = (double)E * 2.0;
      double dry = 2.0 - wet;
      if (wet > 1.0) wet = 1.0; else wet *= wet;
      if (wet < 0.0) wet = 0.0;
      if (dry > 1.0) dry = 1.0;
      if (dry < 0.0) dry = 0.0;

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        float inputSampleL = *in1;
        float inputSampleR = *in2;
        if (fabsf(inputSampleL) < 1.18e-23f) inputSampleL = 1.18e-17f;
        if (fabsf(inputSampleR) < 1.18e-23f) inputSampleR = 1.18e-17f;
        float drySampleL = inputSampleL;
        float drySampleR = inputSampleR;

        // Bezier accumulator path -- stays double for precision
        // at low DeRez where many samples sum into bez_SampL/R.
        bez[cmco_cycle] += derez;
        bez[cmco_SampL] += (((double)inputSampleL + bez[cmco_InL]) * derez);
        bez[cmco_SampR] += (((double)inputSampleR + bez[cmco_InR]) * derez);
        bez[cmco_InL] = (double)inputSampleL;
        bez[cmco_InR] = (double)inputSampleR;

        if (bez[cmco_cycle] > 1.0)
        {
          bez[cmco_cycle] = 0.0;

          // ===== Predelay write + read =====
          aZL[countZ] = (float)bez[cmco_SampL];
          aZR[countZ] = (float)bez[cmco_SampR];
          countZ++; if (countZ < 0 || countZ > adjPredelay) countZ = 0;
          bez[cmco_SampL] = (double)aZL[countZ - ((countZ > adjPredelay) ? adjPredelay + 1 : 0)];
          bez[cmco_SampR] = (double)aZR[countZ - ((countZ > adjPredelay) ? adjPredelay + 1 : 0)];

          // ===== FDN stage 1: input fanout + feedback injection =====
          // sumIn (double) keeps SampL + UnInL accumulation in double;
          // cast to float when stored to the float state array.
          double sumInL = bez[cmco_SampL] + bez[cmco_UnInL];
          double sumInR = bez[cmco_SampR] + bez[cmco_UnInR];
          aAL[countAL] = (float)(sumInL + ((double)feedbackAL * regen));
          aBL[countBL] = (float)(sumInL + ((double)feedbackBL * regen));
          aCL[countCL] = (float)(sumInL + ((double)feedbackCL * regen));
          aDL[countDL] = (float)(sumInL + ((double)feedbackDL * regen));
          bez[cmco_UnInL] = bez[cmco_SampL];

          aDR[countDR] = (float)(sumInR + ((double)feedbackDR * regen));
          aHR[countHR] = (float)(sumInR + ((double)feedbackHR * regen));
          aLR[countLR] = (float)(sumInR + ((double)feedbackLR * regen));
          aPR[countPR] = (float)(sumInR + ((double)feedbackPR * regen));
          bez[cmco_UnInR] = bez[cmco_SampR];

          countAL++; if (countAL < 0 || countAL > shortA) countAL = 0;
          countBL++; if (countBL < 0 || countBL > shortB) countBL = 0;
          countCL++; if (countCL < 0 || countCL > shortC) countCL = 0;
          countDL++; if (countDL < 0 || countDL > shortD) countDL = 0;
          countDR++; if (countDR < 0 || countDR > shortD) countDR = 0;
          countHR++; if (countHR < 0 || countHR > shortH) countHR = 0;
          countLR++; if (countLR < 0 || countLR > shortL) countLR = 0;
          countPR++; if (countPR < 0 || countPR > shortP) countPR = 0;

          float outAL = aAL[countAL - ((countAL > shortA) ? shortA + 1 : 0)];
          float outBL = aBL[countBL - ((countBL > shortB) ? shortB + 1 : 0)];
          float outCL = aCL[countCL - ((countCL > shortC) ? shortC + 1 : 0)];
          float outDL = aDL[countDL - ((countDL > shortD) ? shortD + 1 : 0)];
          float outDR = aDR[countDR - ((countDR > shortD) ? shortD + 1 : 0)];
          float outHR = aHR[countHR - ((countHR > shortH) ? shortH + 1 : 0)];
          float outLR = aLR[countLR - ((countLR > shortL) ? shortL + 1 : 0)];
          float outPR = aPR[countPR - ((countPR > shortP) ? shortP + 1 : 0)];

          // ===== FDN stage 2: 4-line diff-Householder reduction =====
          aEL[countEL] = outAL - (outBL + outCL + outDL);
          aFL[countFL] = outBL - (outAL + outCL + outDL);
          aGL[countGL] = outCL - (outAL + outBL + outDL);
          aHL[countHL] = outDL - (outAL + outBL + outCL);

          aCR[countCR] = outDR - (outHR + outLR + outPR);
          aGR[countGR] = outHR - (outDR + outLR + outPR);
          aKR[countKR] = outLR - (outDR + outHR + outPR);
          aOR[countOR] = outPR - (outDR + outHR + outLR);

          countEL++; if (countEL < 0 || countEL > shortE) countEL = 0;
          countFL++; if (countFL < 0 || countFL > shortF) countFL = 0;
          countGL++; if (countGL < 0 || countGL > shortG) countGL = 0;
          countHL++; if (countHL < 0 || countHL > shortH) countHL = 0;
          countCR++; if (countCR < 0 || countCR > shortC) countCR = 0;
          countGR++; if (countGR < 0 || countGR > shortG) countGR = 0;
          countKR++; if (countKR < 0 || countKR > shortK) countKR = 0;
          countOR++; if (countOR < 0 || countOR > shortO) countOR = 0;

          float outEL = aEL[countEL - ((countEL > shortE) ? shortE + 1 : 0)];
          float outFL = aFL[countFL - ((countFL > shortF) ? shortF + 1 : 0)];
          float outGL = aGL[countGL - ((countGL > shortG) ? shortG + 1 : 0)];
          float outHL = aHL[countHL - ((countHL > shortH) ? shortH + 1 : 0)];
          float outCR = aCR[countCR - ((countCR > shortC) ? shortC + 1 : 0)];
          float outGR = aGR[countGR - ((countGR > shortG) ? shortG + 1 : 0)];
          float outKR = aKR[countKR - ((countKR > shortK) ? shortK + 1 : 0)];
          float outOR = aOR[countOR - ((countOR > shortO) ? shortO + 1 : 0)];

          // ===== FDN stage 3 =====
          aIL[countIL] = outEL - (outFL + outGL + outHL);
          aJL[countJL] = outFL - (outEL + outGL + outHL);
          aKL[countKL] = outGL - (outEL + outFL + outHL);
          aLL[countLL] = outHL - (outEL + outFL + outGL);

          aBR[countBR] = outCR - (outGR + outKR + outOR);
          aFR[countFR] = outGR - (outCR + outKR + outOR);
          aJR[countJR] = outKR - (outCR + outGR + outOR);
          aNR[countNR] = outOR - (outCR + outGR + outKR);

          countIL++; if (countIL < 0 || countIL > shortI) countIL = 0;
          countJL++; if (countJL < 0 || countJL > shortJ) countJL = 0;
          countKL++; if (countKL < 0 || countKL > shortK) countKL = 0;
          countLL++; if (countLL < 0 || countLL > shortL) countLL = 0;
          countBR++; if (countBR < 0 || countBR > shortB) countBR = 0;
          countFR++; if (countFR < 0 || countFR > shortF) countFR = 0;
          countJR++; if (countJR < 0 || countJR > shortJ) countJR = 0;
          countNR++; if (countNR < 0 || countNR > shortN) countNR = 0;

          float outIL = aIL[countIL - ((countIL > shortI) ? shortI + 1 : 0)];
          float outJL = aJL[countJL - ((countJL > shortJ) ? shortJ + 1 : 0)];
          float outKL = aKL[countKL - ((countKL > shortK) ? shortK + 1 : 0)];
          float outLL = aLL[countLL - ((countLL > shortL) ? shortL + 1 : 0)];
          float outBR = aBR[countBR - ((countBR > shortB) ? shortB + 1 : 0)];
          float outFR = aFR[countFR - ((countFR > shortF) ? shortF + 1 : 0)];
          float outJR = aJR[countJR - ((countJR > shortJ) ? shortJ + 1 : 0)];
          float outNR = aNR[countNR - ((countNR > shortN) ? shortN + 1 : 0)];

          // ===== FDN stage 4 =====
          aML[countML] = outIL - (outJL + outKL + outLL);
          aNL[countNL] = outJL - (outIL + outKL + outLL);
          aOL[countOL] = outKL - (outIL + outJL + outLL);
          aPL[countPL] = outLL - (outIL + outJL + outKL);

          aAR[countAR] = outBR - (outFR + outJR + outNR);
          aER[countER] = outFR - (outBR + outJR + outNR);
          aIR[countIR] = outJR - (outBR + outFR + outNR);
          aMR[countMR] = outNR - (outBR + outFR + outJR);

          countML++; if (countML < 0 || countML > shortM) countML = 0;
          countNL++; if (countNL < 0 || countNL > shortN) countNL = 0;
          countOL++; if (countOL < 0 || countOL > shortO) countOL = 0;
          countPL++; if (countPL < 0 || countPL > shortP) countPL = 0;
          countAR++; if (countAR < 0 || countAR > shortA) countAR = 0;
          countER++; if (countER < 0 || countER > shortE) countER = 0;
          countIR++; if (countIR < 0 || countIR > shortI) countIR = 0;
          countMR++; if (countMR < 0 || countMR > shortM) countMR = 0;

          float outML = aML[countML - ((countML > shortM) ? shortM + 1 : 0)];
          float outNL = aNL[countNL - ((countNL > shortN) ? shortN + 1 : 0)];
          float outOL = aOL[countOL - ((countOL > shortO) ? shortO + 1 : 0)];
          float outPL = aPL[countPL - ((countPL > shortP) ? shortP + 1 : 0)];
          float outAR = aAR[countAR - ((countAR > shortA) ? shortA + 1 : 0)];
          float outER = aER[countER - ((countER > shortE) ? shortE + 1 : 0)];
          float outIR = aIR[countIR - ((countIR > shortI) ? shortI + 1 : 0)];
          float outMR = aMR[countMR - ((countMR > shortM) ? shortM + 1 : 0)];

          // Capture cross-channel feedback taps.
          feedbackAL = outML - (outNL + outOL + outPL);
          feedbackDR = outAR - (outER + outIR + outMR);
          feedbackBL = outNL - (outML + outOL + outPL);
          feedbackHR = outER - (outAR + outIR + outMR);
          feedbackCL = outOL - (outML + outNL + outPL);
          feedbackLR = outIR - (outAR + outER + outMR);
          feedbackDL = outPL - (outML + outNL + outOL);
          feedbackPR = outMR - (outAR + outER + outIR);

          // Output sum (corrected for Householder gain + averaging).
          inputSampleL = (outML + outNL + outOL + outPL) / 32.0f;
          inputSampleR = (outAR + outER + outIR + outMR) / 32.0f;

          // Shift the bezier ABC history. Cast to double on write.
          bez[cmco_CL] = bez[cmco_BL];
          bez[cmco_BL] = bez[cmco_AL];
          bez[cmco_AL] = (double)inputSampleL;
          bez[cmco_SampL] = 0.0;

          bez[cmco_CR] = bez[cmco_BR];
          bez[cmco_BR] = bez[cmco_AR];
          bez[cmco_AR] = (double)inputSampleR;
          bez[cmco_SampR] = 0.0;
        }

        // ----- Bezier reconstruction (every input sample) -----
        // Stays in double end-to-end since bez[] is double.
        double cyc = bez[cmco_cycle];
        double oneMinusCyc = 1.0 - cyc;
        double CBL = (bez[cmco_CL] * oneMinusCyc) + (bez[cmco_BL] * cyc);
        double CBR = (bez[cmco_CR] * oneMinusCyc) + (bez[cmco_BR] * cyc);
        double BAL = (bez[cmco_BL] * oneMinusCyc) + (bez[cmco_AL] * cyc);
        double BAR = (bez[cmco_BR] * oneMinusCyc) + (bez[cmco_AR] * cyc);
        double CBAL = (bez[cmco_BL] + (CBL * oneMinusCyc) + (BAL * cyc)) * 0.125;
        double CBAR = (bez[cmco_BR] + (CBR * oneMinusCyc) + (BAR * cyc)) * 0.125;
        inputSampleL = (float)CBAL;
        inputSampleR = (float)CBAR;

        // Output clipping at ±1
        if (inputSampleL > 1.0f) inputSampleL = 1.0f;
        if (inputSampleL < -1.0f) inputSampleL = -1.0f;
        if (inputSampleR > 1.0f) inputSampleR = 1.0f;
        if (inputSampleR < -1.0f) inputSampleR = -1.0f;

        // Submix wet/dry: 0.5 = both at full (sum, no crossfade)
        if (wet < 1.0) { inputSampleL *= (float)wet; inputSampleR *= (float)wet; }
        if (dry < 1.0) { drySampleL *= (float)dry; drySampleR *= (float)dry; }
        inputSampleL += drySampleL;
        inputSampleR += drySampleR;

        *out1 = inputSampleL;
        *out2 = inputSampleR;

        in1++;
        in2++;
        out1++;
        out2++;
      }
    }

  private:
    // ===== State (Phase 1 hybrid float-conversion) =====

    // 4x4 FDN delay arrays, per side (16 lines x 2 = 32 arrays).
    // FLOAT per hybrid float-conversion (was double in AW source).
    float aAL[kshortA + 5], aAR[kshortA + 5];
    float aBL[kshortB + 5], aBR[kshortB + 5];
    float aCL[kshortC + 5], aCR[kshortC + 5];
    float aDL[kshortD + 5], aDR[kshortD + 5];
    float aEL[kshortE + 5], aER[kshortE + 5];
    float aFL[kshortF + 5], aFR[kshortF + 5];
    float aGL[kshortG + 5], aGR[kshortG + 5];
    float aHL[kshortH + 5], aHR[kshortH + 5];
    float aIL[kshortI + 5], aIR[kshortI + 5];
    float aJL[kshortJ + 5], aJR[kshortJ + 5];
    float aKL[kshortK + 5], aKR[kshortK + 5];
    float aLL[kshortL + 5], aLR[kshortL + 5];
    float aML[kshortM + 5], aMR[kshortM + 5];
    float aNL[kshortN + 5], aNR[kshortN + 5];
    float aOL[kshortO + 5], aOR[kshortO + 5];
    float aPL[kshortP + 5], aPR[kshortP + 5];

    // Predelay buffer (15000 samples per side). FLOAT.
    float aZL[kCreamCoatPredelay + 5];
    float aZR[kCreamCoatPredelay + 5];

    // Cross-channel feedback taps. FLOAT.
    float feedbackAL, feedbackBL, feedbackCL, feedbackDL;
    float feedbackDR, feedbackHR, feedbackLR, feedbackPR;

    // Counters.
    int countAL, countBL, countCL, countDL, countEL, countFL, countGL, countHL;
    int countIL, countJL, countKL, countLL, countML, countNL, countOL, countPL;
    int countAR, countBR, countCR, countDR, countER, countFR, countGR, countHR;
    int countIR, countJR, countKR, countLR, countMR, countNR, countOR, countPR;
    int countZ;

    // Bezier-undersample state — DOUBLE (accumulator path,
    // precision-critical at low DeRez).
    double bez[cmco_total];

    // Current per-line delay lengths (loaded from preset on Select change).
    int shortA, shortB, shortC, shortD, shortE, shortF, shortG, shortH;
    int shortI, shortJ, shortK, shortL, shortM, shortN, shortO, shortP;

    int prevclearcoat;
#endif
  };

} // namespace house
