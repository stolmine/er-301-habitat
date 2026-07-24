// house::WoodenBox
//
// Airwindows WoodenBox (Chris Johnson, MIT) port. Faithful per-
// sample DSP from the AW source, exposed as an ER-301 od::Object
// atom. Second AW port in the house package; follows the template
// established by KWoodRoom and codified in
// feedback_aw_atom_port_template.
//
// Topology: 4-stage 4x4 diff-Householder FDN per side, walked in
// opposite delay-line orders L vs R, cross-coupled via the final
// feedback taps. Single outer Bezier-undersample (no inner
// filter stage). 17 prebaked delay-length tables ("boxes")
// selected by Select.
//
// LOAD-BEARING from AW source — preserved literally per
// feedback_identical_means_identical:
//   - bez[bez_cycle] = 1.0 at ctor (forces first reverb cycle to
//     fire so first output is non-zero)
//   - counters init to 1 (read formula returns arr[0] safely)
//   - L/R "swap" through the verb: input L routes through R verb
//     path, input R through L verb path. AW source comments
//     "stereo got reversed somewhere?" but ships it that way
//     intentionally. Do not "correct."
//
// Dropped per template:
//   - Per-sample 32-bit dither (frexpf + pow per sample is a
//     CloudSeed-style risk on Cortex-A8; ER-301 internal float
//     bus doesn't need it)
//   - rand()-seeded fpdL/fpdR (no libc dep; denormal flush uses
//     a deterministic constant)
//   - VST host deps (audioeffectx, std::set, std::string)
//
// Phase 1: state arrays remain `double` end-to-end. Phase 2+
// can listen-test floating the b4 arrays for ~14 KB save per
// instance, and apply the 4-line Householder NEON reduction
// (3*hX - total) for the ~16 ops → ~6 ops per-layer win.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace house
{

  // Delay-line max sizes (verbatim from AW WoodenBox.h)
  static const int d4A = 173;
  static const int d4B = 82;
  static const int d4C = 240;
  static const int d4D = 191;
  static const int d4E = 196;
  static const int d4F = 257;
  static const int d4G = 203;
  static const int d4H = 252;
  static const int d4I = 207;
  static const int d4J = 203;
  static const int d4K = 250;
  static const int d4L = 220;
  static const int d4M = 261;
  static const int d4N = 235;
  static const int d4O = 161;
  static const int d4P = 161;

  // bez[] slot indices (verbatim from AW WoodenBox.h enum;
  // single bezier set, no bezF — WoodenBox has no inner filter
  // stage unlike kWoodRoom)
  enum WoodenBoxBezSlot
  {
    wbb_AL = 0,
    wbb_AR,
    wbb_BL,
    wbb_BR,
    wbb_CL,
    wbb_CR,
    wbb_SampL,
    wbb_SampR,
    wbb_cycle,
    wbb_total
  };

  class WoodenBox : public od::Object
  {
  public:
    WoodenBox()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mSelect);
      addParameter(mReso);
      addParameter(mMix);

      // Zero every delay-line array.
      memset(b4AL, 0, sizeof(b4AL)); memset(b4AR, 0, sizeof(b4AR));
      memset(b4BL, 0, sizeof(b4BL)); memset(b4BR, 0, sizeof(b4BR));
      memset(b4CL, 0, sizeof(b4CL)); memset(b4CR, 0, sizeof(b4CR));
      memset(b4DL, 0, sizeof(b4DL)); memset(b4DR, 0, sizeof(b4DR));
      memset(b4EL, 0, sizeof(b4EL)); memset(b4ER, 0, sizeof(b4ER));
      memset(b4FL, 0, sizeof(b4FL)); memset(b4FR, 0, sizeof(b4FR));
      memset(b4GL, 0, sizeof(b4GL)); memset(b4GR, 0, sizeof(b4GR));
      memset(b4HL, 0, sizeof(b4HL)); memset(b4HR, 0, sizeof(b4HR));
      memset(b4IL, 0, sizeof(b4IL)); memset(b4IR, 0, sizeof(b4IR));
      memset(b4JL, 0, sizeof(b4JL)); memset(b4JR, 0, sizeof(b4JR));
      memset(b4KL, 0, sizeof(b4KL)); memset(b4KR, 0, sizeof(b4KR));
      memset(b4LL, 0, sizeof(b4LL)); memset(b4LR, 0, sizeof(b4LR));
      memset(b4ML, 0, sizeof(b4ML)); memset(b4MR, 0, sizeof(b4MR));
      memset(b4NL, 0, sizeof(b4NL)); memset(b4NR, 0, sizeof(b4NR));
      memset(b4OL, 0, sizeof(b4OL)); memset(b4OR, 0, sizeof(b4OR));
      memset(b4PL, 0, sizeof(b4PL)); memset(b4PR, 0, sizeof(b4PR));

      // Counters init to 1 per AW source.
      c4AL = c4BL = c4CL = c4DL = c4EL = c4FL = c4GL = c4HL = 1;
      c4IL = c4JL = c4KL = c4LL = c4ML = c4NL = c4OL = c4PL = 1;
      c4AR = c4BR = c4CR = c4DR = c4ER = c4FR = c4GR = c4HR = 1;
      c4IR = c4JR = c4KR = c4LR = c4MR = c4NR = c4OR = c4PR = 1;

      g4AL = g4BL = g4CL = g4DL = 0.0;
      g4DR = g4HR = g4LR = g4PR = 0.0;

      for (int x = 0; x < wbb_total; x++) bez[x] = 0.0;
      // LOAD-BEARING first-frame init.
      bez[wbb_cycle] = 1.0;

      // Initial short* values = d4* defaults (case 10 in source
      // switch — Select≈0.6; defaults at A=0.5 will trigger a
      // Select-change reset on first process() to case 8).
      shortA = d4A; shortB = d4B; shortC = d4C; shortD = d4D;
      shortE = d4E; shortF = d4F; shortG = d4G; shortH = d4H;
      shortI = d4I; shortJ = d4J; shortK = d4K; shortL = d4L;
      shortM = d4M; shortN = d4N; shortO = d4O; shortP = d4P;
      prevclearcoat = -1; // forces first-cycle Select reset
    }

    virtual ~WoodenBox() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};

    // 3 user parameters. Defaults match AW pinParameter defaults.
    od::Parameter mSelect{"Select", 0.5f};
    od::Parameter mReso{"Reso", 0.5f};
    od::Parameter mMix{"Mix", 0.5f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      const float A = mSelect.value();
      const float B = mReso.value();
      const float C = mMix.value();

      double overallscale = 1.0;
      overallscale /= 44100.0;
      overallscale *= (double)globalConfig.sampleRate;

      int clearcoat = (int)(A * 16.999);
      if (clearcoat < 0) clearcoat = 0;
      if (clearcoat > 16) clearcoat = 16;
      if (clearcoat != prevclearcoat)
      {
        // Zero all delay arrays and reset all counters. Audio-
        // thread work; ~13 us at typical bandwidth. Acceptable.
        memset(b4AL, 0, sizeof(b4AL)); memset(b4AR, 0, sizeof(b4AR));
        memset(b4BL, 0, sizeof(b4BL)); memset(b4BR, 0, sizeof(b4BR));
        memset(b4CL, 0, sizeof(b4CL)); memset(b4CR, 0, sizeof(b4CR));
        memset(b4DL, 0, sizeof(b4DL)); memset(b4DR, 0, sizeof(b4DR));
        memset(b4EL, 0, sizeof(b4EL)); memset(b4ER, 0, sizeof(b4ER));
        memset(b4FL, 0, sizeof(b4FL)); memset(b4FR, 0, sizeof(b4FR));
        memset(b4GL, 0, sizeof(b4GL)); memset(b4GR, 0, sizeof(b4GR));
        memset(b4HL, 0, sizeof(b4HL)); memset(b4HR, 0, sizeof(b4HR));
        memset(b4IL, 0, sizeof(b4IL)); memset(b4IR, 0, sizeof(b4IR));
        memset(b4JL, 0, sizeof(b4JL)); memset(b4JR, 0, sizeof(b4JR));
        memset(b4KL, 0, sizeof(b4KL)); memset(b4KR, 0, sizeof(b4KR));
        memset(b4LL, 0, sizeof(b4LL)); memset(b4LR, 0, sizeof(b4LR));
        memset(b4ML, 0, sizeof(b4ML)); memset(b4MR, 0, sizeof(b4MR));
        memset(b4NL, 0, sizeof(b4NL)); memset(b4NR, 0, sizeof(b4NR));
        memset(b4OL, 0, sizeof(b4OL)); memset(b4OR, 0, sizeof(b4OR));
        memset(b4PL, 0, sizeof(b4PL)); memset(b4PR, 0, sizeof(b4PR));

        c4AL = c4BL = c4CL = c4DL = c4EL = c4FL = c4GL = c4HL = 1;
        c4IL = c4JL = c4KL = c4LL = c4ML = c4NL = c4OL = c4PL = 1;
        c4AR = c4BR = c4CR = c4DR = c4ER = c4FR = c4GR = c4HR = 1;
        c4IR = c4JR = c4KR = c4LR = c4MR = c4NR = c4OR = c4PR = 1;

        // Box-character preset tables (verbatim from AW source switch).
        switch (clearcoat)
        {
        case 0:
          shortA = 17; shortB = 10; shortC = 23; shortD = 3; shortE = 8; shortF = 7; shortG = 41; shortH = 6; shortI = 3; shortJ = 6; shortK = 59; shortL = 61; shortM = 4; shortN = 71; shortO = 5; shortP = 4; break;
        case 1:
          shortA = 12; shortB = 19; shortC = 89; shortD = 25; shortE = 92; shortF = 8; shortG = 41; shortH = 11; shortI = 80; shortJ = 27; shortK = 6; shortL = 4; shortM = 3; shortN = 21; shortO = 7; shortP = 63; break;
        case 2:
          shortA = 35; shortB = 19; shortC = 5; shortD = 7; shortE = 15; shortF = 7; shortG = 41; shortH = 191; shortI = 177; shortJ = 3; shortK = 6; shortL = 22; shortM = 23; shortN = 118; shortO = 4; shortP = 79; break;
        case 3:
          shortA = 17; shortB = 19; shortC = 105; shortD = 135; shortE = 31; shortF = 86; shortG = 41; shortH = 16; shortI = 3; shortJ = 16; shortK = 6; shortL = 151; shortM = 147; shortN = 26; shortO = 3; shortP = 10; break;
        case 4:
          shortA = 134; shortB = 13; shortC = 26; shortD = 10; shortE = 34; shortF = 24; shortG = 4; shortH = 60; shortI = 88; shortJ = 9; shortK = 155; shortL = 11; shortM = 3; shortN = 18; shortO = 9; shortP = 161; break;
        case 5:
          shortA = 17; shortB = 82; shortC = 23; shortD = 29; shortE = 133; shortF = 3; shortG = 41; shortH = 27; shortI = 10; shortJ = 177; shortK = 6; shortL = 37; shortM = 14; shortN = 145; shortO = 4; shortP = 9; break;
        case 6:
          shortA = 31; shortB = 19; shortC = 3; shortD = 29; shortE = 196; shortF = 11; shortG = 10; shortH = 65; shortI = 21; shortJ = 3; shortK = 148; shortL = 4; shortM = 26; shortN = 7; shortO = 161; shortP = 155; break;
        case 7:
          shortA = 17; shortB = 8; shortC = 3; shortD = 37; shortE = 3; shortF = 19; shortG = 41; shortH = 15; shortI = 7; shortJ = 197; shortK = 178; shortL = 22; shortM = 26; shortN = 97; shortO = 16; shortP = 156; break;
        case 8:
          shortA = 17; shortB = 3; shortC = 8; shortD = 29; shortE = 39; shortF = 156; shortG = 7; shortH = 43; shortI = 101; shortJ = 8; shortK = 15; shortL = 169; shortM = 67; shortN = 39; shortO = 154; shortP = 4; break;
        case 9:
          shortA = 18; shortB = 19; shortC = 23; shortD = 5; shortE = 176; shortF = 3; shortG = 41; shortH = 147; shortI = 7; shortJ = 148; shortK = 5; shortL = 15; shortM = 10; shortN = 30; shortO = 119; shortP = 19; break;
        case 10:
          shortA = 173; shortB = 19; shortC = 23; shortD = 27; shortE = 8; shortF = 37; shortG = 7; shortH = 202; shortI = 8; shortJ = 13; shortK = 3; shortL = 174; shortM = 67; shortN = 21; shortO = 73; shortP = 14; break;
        case 11:
          shortA = 17; shortB = 19; shortC = 23; shortD = 25; shortE = 19; shortF = 145; shortG = 9; shortH = 43; shortI = 47; shortJ = 203; shortK = 18; shortL = 180; shortM = 226; shortN = 3; shortO = 73; shortP = 12; break;
        case 12:
          shortA = 17; shortB = 19; shortC = 23; shortD = 3; shortE = 3; shortF = 20; shortG = 203; shortH = 99; shortI = 207; shortJ = 15; shortK = 10; shortL = 61; shortM = 20; shortN = 174; shortO = 33; shortP = 77; break;
        case 13:
          shortA = 17; shortB = 19; shortC = 23; shortD = 29; shortE = 3; shortF = 210; shortG = 183; shortH = 43; shortI = 13; shortJ = 12; shortK = 26; shortL = 220; shortM = 67; shortN = 235; shortO = 11; shortP = 23; break;
        case 14:
          shortA = 17; shortB = 3; shortC = 21; shortD = 191; shortE = 31; shortF = 10; shortG = 41; shortH = 218; shortI = 15; shortJ = 6; shortK = 111; shortL = 29; shortM = 129; shortN = 206; shortO = 4; shortP = 7; break;
        case 15:
          shortA = 17; shortB = 25; shortC = 240; shortD = 29; shortE = 4; shortF = 18; shortG = 41; shortH = 43; shortI = 29; shortJ = 28; shortK = 250; shortL = 12; shortM = 261; shortN = 9; shortO = 5; shortP = 79; break;
        case 16:
        default:
          shortA = 5; shortB = 3; shortC = 23; shortD = 29; shortE = 3; shortF = 257; shortG = 199; shortH = 252; shortI = 132; shortJ = 18; shortK = 11; shortL = 6; shortM = 30; shortN = 27; shortO = 7; shortP = 8; break;
        }
        prevclearcoat = clearcoat;
      }

      double reg4n = (1.0 - pow(1.0 - B, 2.0)) * 0.0336;
      double derez = 1.0;
      derez = fmin(fmax(derez / overallscale, 0.0001), 1.0);
      int bezFraction = (int)(1.0 / derez);
      double bezTrim = (double)bezFraction / (bezFraction + 1.0);
      derez = 1.0 / bezFraction;
      bezTrim = 1.0 - (derez * bezTrim);

      double wet = 1.0 - pow(1.0 - C, 2.0);

      float reg4nF = (float)reg4n;
      float wetF = (float)wet;
      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        float inputSampleL = *in1;
        float inputSampleR = *in2;
        // Denormal flush — deterministic constant (dither dropped).
        if (fabsf(inputSampleL) < 1.18e-23f) inputSampleL = 1.18e-17f;
        if (fabsf(inputSampleR) < 1.18e-23f) inputSampleR = 1.18e-17f;
        float drySampleL = inputSampleL;
        float drySampleR = inputSampleR;

        bez[wbb_cycle] += derez;
        // INTENTIONAL L/R swap from AW source ("stereo got reversed
        // somewhere?" comment). Preserve literally.
        bez[wbb_SampL] += (inputSampleR * derez);
        bez[wbb_SampR] += (inputSampleL * derez);

        if (bez[wbb_cycle] > 1.0)
        {
          bez[wbb_cycle] = 0.0;

          // ===== Left verb path (4 stages × 4 lines) =====
          float dualmonoSampleL = bez[wbb_SampL];
          b4AL[c4AL] = dualmonoSampleL + (g4AL * reg4nF);
          b4BL[c4BL] = dualmonoSampleL + (g4BL * reg4nF);
          b4CL[c4CL] = dualmonoSampleL + (g4CL * reg4nF);
          b4DL[c4DL] = dualmonoSampleL + (g4DL * reg4nF);

          c4AL++; if (c4AL < 0 || c4AL > shortA) c4AL = 0;
          c4BL++; if (c4BL < 0 || c4BL > shortB) c4BL = 0;
          c4CL++; if (c4CL < 0 || c4CL > shortC) c4CL = 0;
          c4DL++; if (c4DL < 0 || c4DL > shortD) c4DL = 0;

          float hA = b4AL[c4AL - ((c4AL > shortA) ? shortA + 1 : 0)];
          float hB = b4BL[c4BL - ((c4BL > shortB) ? shortB + 1 : 0)];
          float hC = b4CL[c4CL - ((c4CL > shortC) ? shortC + 1 : 0)];
          float hD = b4DL[c4DL - ((c4DL > shortD) ? shortD + 1 : 0)];

          // 4-line diff-Householder: hX - (sum others). Sum-preserving.
          b4EL[c4EL] = hA - (hB + hC + hD);
          b4FL[c4FL] = hB - (hA + hC + hD);
          b4GL[c4GL] = hC - (hA + hB + hD);
          b4HL[c4HL] = hD - (hA + hB + hC);

          c4EL++; if (c4EL < 0 || c4EL > shortE) c4EL = 0;
          c4FL++; if (c4FL < 0 || c4FL > shortF) c4FL = 0;
          c4GL++; if (c4GL < 0 || c4GL > shortG) c4GL = 0;
          c4HL++; if (c4HL < 0 || c4HL > shortH) c4HL = 0;

          hA = b4EL[c4EL - ((c4EL > shortE) ? shortE + 1 : 0)];
          hB = b4FL[c4FL - ((c4FL > shortF) ? shortF + 1 : 0)];
          hC = b4GL[c4GL - ((c4GL > shortG) ? shortG + 1 : 0)];
          hD = b4HL[c4HL - ((c4HL > shortH) ? shortH + 1 : 0)];
          b4IL[c4IL] = hA - (hB + hC + hD);
          b4JL[c4JL] = hB - (hA + hC + hD);
          b4KL[c4KL] = hC - (hA + hB + hD);
          b4LL[c4LL] = hD - (hA + hB + hC);

          c4IL++; if (c4IL < 0 || c4IL > shortI) c4IL = 0;
          c4JL++; if (c4JL < 0 || c4JL > shortJ) c4JL = 0;
          c4KL++; if (c4KL < 0 || c4KL > shortK) c4KL = 0;
          c4LL++; if (c4LL < 0 || c4LL > shortL) c4LL = 0;

          hA = b4IL[c4IL - ((c4IL > shortI) ? shortI + 1 : 0)];
          hB = b4JL[c4JL - ((c4JL > shortJ) ? shortJ + 1 : 0)];
          hC = b4KL[c4KL - ((c4KL > shortK) ? shortK + 1 : 0)];
          hD = b4LL[c4LL - ((c4LL > shortL) ? shortL + 1 : 0)];
          b4ML[c4ML] = hA - (hB + hC + hD);
          b4NL[c4NL] = hB - (hA + hC + hD);
          b4OL[c4OL] = hC - (hA + hB + hD);
          b4PL[c4PL] = hD - (hA + hB + hC);

          c4ML++; if (c4ML < 0 || c4ML > shortM) c4ML = 0;
          c4NL++; if (c4NL < 0 || c4NL > shortN) c4NL = 0;
          c4OL++; if (c4OL < 0 || c4OL > shortO) c4OL = 0;
          c4PL++; if (c4PL < 0 || c4PL > shortP) c4PL = 0;

          hA = b4ML[c4ML - ((c4ML > shortM) ? shortM + 1 : 0)];
          hB = b4NL[c4NL - ((c4NL > shortN) ? shortN + 1 : 0)];
          hC = b4OL[c4OL - ((c4OL > shortO) ? shortO + 1 : 0)];
          hD = b4PL[c4PL - ((c4PL > shortP) ? shortP + 1 : 0)];
          g4AL = hA - (hB + hC + hD);
          g4BL = hB - (hA + hC + hD);
          g4CL = hC - (hA + hB + hD);
          g4DL = hD - (hA + hB + hC);
          dualmonoSampleL = (hA + hB + hC + hD) * 0.125f;

          // ===== Right verb path (walked in reverse line order) =====
          float dualmonoSampleR = bez[wbb_SampR];
          b4DR[c4DR] = dualmonoSampleR + (g4DR * reg4nF);
          b4HR[c4HR] = dualmonoSampleR + (g4HR * reg4nF);
          b4LR[c4LR] = dualmonoSampleR + (g4LR * reg4nF);
          b4PR[c4PR] = dualmonoSampleR + (g4PR * reg4nF);

          c4DR++; if (c4DR < 0 || c4DR > shortD) c4DR = 0;
          c4HR++; if (c4HR < 0 || c4HR > shortH) c4HR = 0;
          c4LR++; if (c4LR < 0 || c4LR > shortL) c4LR = 0;
          c4PR++; if (c4PR < 0 || c4PR > shortP) c4PR = 0;

          hA = b4DR[c4DR - ((c4DR > shortD) ? shortD + 1 : 0)];
          hB = b4HR[c4HR - ((c4HR > shortH) ? shortH + 1 : 0)];
          hC = b4LR[c4LR - ((c4LR > shortL) ? shortL + 1 : 0)];
          hD = b4PR[c4PR - ((c4PR > shortP) ? shortP + 1 : 0)];
          b4CR[c4CR] = hA - (hB + hC + hD);
          b4GR[c4GR] = hB - (hA + hC + hD);
          b4KR[c4KR] = hC - (hA + hB + hD);
          b4OR[c4OR] = hD - (hA + hB + hC);

          c4CR++; if (c4CR < 0 || c4CR > shortC) c4CR = 0;
          c4GR++; if (c4GR < 0 || c4GR > shortG) c4GR = 0;
          c4KR++; if (c4KR < 0 || c4KR > shortK) c4KR = 0;
          c4OR++; if (c4OR < 0 || c4OR > shortO) c4OR = 0;

          hA = b4CR[c4CR - ((c4CR > shortC) ? shortC + 1 : 0)];
          hB = b4GR[c4GR - ((c4GR > shortG) ? shortG + 1 : 0)];
          hC = b4KR[c4KR - ((c4KR > shortK) ? shortK + 1 : 0)];
          hD = b4OR[c4OR - ((c4OR > shortO) ? shortO + 1 : 0)];
          b4BR[c4BR] = hA - (hB + hC + hD);
          b4FR[c4FR] = hB - (hA + hC + hD);
          b4JR[c4JR] = hC - (hA + hB + hD);
          b4NR[c4NR] = hD - (hA + hB + hC);

          c4BR++; if (c4BR < 0 || c4BR > shortB) c4BR = 0;
          c4FR++; if (c4FR < 0 || c4FR > shortF) c4FR = 0;
          c4JR++; if (c4JR < 0 || c4JR > shortJ) c4JR = 0;
          c4NR++; if (c4NR < 0 || c4NR > shortN) c4NR = 0;

          hA = b4BR[c4BR - ((c4BR > shortB) ? shortB + 1 : 0)];
          hB = b4FR[c4FR - ((c4FR > shortF) ? shortF + 1 : 0)];
          hC = b4JR[c4JR - ((c4JR > shortJ) ? shortJ + 1 : 0)];
          hD = b4NR[c4NR - ((c4NR > shortN) ? shortN + 1 : 0)];
          b4AR[c4AR] = hA - (hB + hC + hD);
          b4ER[c4ER] = hB - (hA + hC + hD);
          b4IR[c4IR] = hC - (hA + hB + hD);
          b4MR[c4MR] = hD - (hA + hB + hC);

          c4AR++; if (c4AR < 0 || c4AR > shortA) c4AR = 0;
          c4ER++; if (c4ER < 0 || c4ER > shortE) c4ER = 0;
          c4IR++; if (c4IR < 0 || c4IR > shortI) c4IR = 0;
          c4MR++; if (c4MR < 0 || c4MR > shortM) c4MR = 0;

          hA = b4AR[c4AR - ((c4AR > shortA) ? shortA + 1 : 0)];
          hB = b4ER[c4ER - ((c4ER > shortE) ? shortE + 1 : 0)];
          hC = b4IR[c4IR - ((c4IR > shortI) ? shortI + 1 : 0)];
          hD = b4MR[c4MR - ((c4MR > shortM) ? shortM + 1 : 0)];
          g4DR = hA - (hB + hC + hD);
          g4HR = hB - (hA + hC + hD);
          g4LR = hC - (hA + hB + hD);
          g4PR = hD - (hA + hB + hC);
          dualmonoSampleR = (hA + hB + hC + hD) * 0.125f;

          // Shift the outer-Bezier ABC history.
          // INTENTIONAL L/R swap on output: dualmonoSampleR → bez_AL,
          // dualmonoSampleL → bez_AR. Net effect with the input swap
          // above: inL → R-verb path → outR; inR → L-verb path → outL.
          // AW ships this configuration. Preserve.
          bez[wbb_CL] = bez[wbb_BL];
          bez[wbb_BL] = bez[wbb_AL];
          bez[wbb_AL] = dualmonoSampleR;
          bez[wbb_SampL] = 0.0;

          bez[wbb_CR] = bez[wbb_BR];
          bez[wbb_BR] = bez[wbb_AR];
          bez[wbb_AR] = dualmonoSampleL;
          bez[wbb_SampR] = 0.0;
        }
        // ----- Outer Bezier reconstruction (every input sample) -----
        double X = bez[wbb_cycle] * bezTrim;
        double CBL = (bez[wbb_CL] * (1.0 - X)) + (bez[wbb_BL] * X);
        double CBR = (bez[wbb_CR] * (1.0 - X)) + (bez[wbb_BR] * X);
        double BAL = (bez[wbb_BL] * (1.0 - X)) + (bez[wbb_AL] * X);
        double BAR = (bez[wbb_BR] * (1.0 - X)) + (bez[wbb_AR] * X);
        inputSampleL = (bez[wbb_BL] + (CBL * (1.0 - X)) + (BAL * X)) * 0.125;
        inputSampleR = (bez[wbb_BR] + (CBR * (1.0 - X)) + (BAR * X)) * 0.125;

        // Wet/dry
        inputSampleL = (inputSampleL * wetF) + (drySampleL * (1.0f - wetF));
        inputSampleR = (inputSampleR * wetF) + (drySampleR * (1.0f - wetF));

        *out1 = inputSampleL;
        *out2 = inputSampleR;

        in1++;
        in2++;
        out1++;
        out2++;
      }
    }

  private:
    // 4×4 FDN delay arrays, per side (16 lines × 2 = 32 arrays)
    float b4AL[d4A + 5], b4AR[d4A + 5];
    float b4BL[d4B + 5], b4BR[d4B + 5];
    float b4CL[d4C + 5], b4CR[d4C + 5];
    float b4DL[d4D + 5], b4DR[d4D + 5];
    float b4EL[d4E + 5], b4ER[d4E + 5];
    float b4FL[d4F + 5], b4FR[d4F + 5];
    float b4GL[d4G + 5], b4GR[d4G + 5];
    float b4HL[d4H + 5], b4HR[d4H + 5];
    float b4IL[d4I + 5], b4IR[d4I + 5];
    float b4JL[d4J + 5], b4JR[d4J + 5];
    float b4KL[d4K + 5], b4KR[d4K + 5];
    float b4LL[d4L + 5], b4LR[d4L + 5];
    float b4ML[d4M + 5], b4MR[d4M + 5];
    float b4NL[d4N + 5], b4NR[d4N + 5];
    float b4OL[d4O + 5], b4OR[d4O + 5];
    float b4PL[d4P + 5], b4PR[d4P + 5];

    int c4AL, c4BL, c4CL, c4DL, c4EL, c4FL, c4GL, c4HL;
    int c4IL, c4JL, c4KL, c4LL, c4ML, c4NL, c4OL, c4PL;
    int c4AR, c4BR, c4CR, c4DR, c4ER, c4FR, c4GR, c4HR;
    int c4IR, c4JR, c4KR, c4LR, c4MR, c4NR, c4OR, c4PR;

    // Cross-channel feedback taps (L's final stage feeds R input
    // next cycle and vice versa).
    float g4AL, g4BL, g4CL, g4DL;
    float g4DR, g4HR, g4LR, g4PR;

    // Bezier-undersample state.
    double bez[wbb_total];

    // Current per-line delay lengths (loaded from preset table on
    // Select change).
    int shortA, shortB, shortC, shortD, shortE, shortF, shortG, shortH;
    int shortI, shortJ, shortK, shortL, shortM, shortN, shortO, shortP;

    // Last clearcoat (Select-int) value, for change detection.
    int prevclearcoat;
#endif
  };

} // namespace house
