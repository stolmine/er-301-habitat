// house::KWoodRoom
//
// AW kWoodRoom (Chris Johnson, MIT) as an ER-301 od::Object atom.
// Phase 1 of the port: faithful per-sample DSP from the Phase 0
// helper (KWoodRoomDSP), now wrapped as a proper Object with
// inlets / outlets / parameters consumed by the audio graph.
//
// Per planning/house-atom-architecture.md: header-only Object
// subclass in mods/house/atoms/. All virtuals inline.
//
// Per planning/kwoodroom-port-plan.md: dither dropped (per-sample
// pow() is a CloudSeed-style hang risk on Cortex-A8 and the
// ER-301 internal float bus doesn't need it). State arrays kept
// as `double` end-to-end for Phase 1; per-channel `float`-ing
// and the NEON Householder reduction land in later phases.
//
// LOAD-BEARING: bez[bez_cycle] = 1.0 and bezF[bez_cycle] = 1.0
// at construction force the first reverb cycle to fire so first
// output is non-zero. Do NOT initialize these to 0. Per
// feedback_identical_means_identical.
//
// Heap-allocated by addObject(); state arrays (~113 KB) are
// class members so they live on the heap, never the audio-thread
// stack. Per feedback_neon_intrinsics_drumvoice.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace house
{

  // Delay-line sizes (verbatim from AW kWoodRoom.h).
  // 3x3 trellis (early reflections):
  static const int d3A = 581;
  static const int d3B = 831;
  static const int d3C = 832;
  static const int d3D = 574;
  static const int d3E = 598;
  static const int d3F = 685;
  static const int d3G = 499;
  static const int d3H = 573;
  static const int d3I = 655;

  // 6x6 trellis (main reverb), 249-seat club tuning:
  static const int d6A = 154; static const int d6B = 832; static const int d6C = 109;
  static const int d6D = 685; static const int d6E = 33;  static const int d6F = 12;
  static const int d6G = 27;  static const int d6H = 30;  static const int d6I = 339;
  static const int d6J = 499; static const int d6K = 296; static const int d6L = 169;
  static const int d6M = 169; static const int d6N = 831; static const int d6O = 15;
  static const int d6P = 411; static const int d6Q = 238; static const int d6R = 68;
  static const int d6S = 0;   static const int d6T = 8;   static const int d6U = 655;
  static const int d6V = 581; static const int d6W = 465; static const int d6X = 173;
  static const int d6Y = 3;   static const int d6ZA = 96; static const int d6ZB = 573;
  static const int d6ZC = 243; static const int d6ZD = 30; static const int d6ZE = 188;
  static const int d6ZF = 291; static const int d6ZG = 11; static const int d6ZH = 372;
  static const int d6ZI = 574; static const int d6ZJ = 100; static const int d6ZK = 598;

  // Early-reflection delay-length table (36 entries). Param E
  // (Position) picks a 9-wide window starting at start = (int)(E*27).
  static constexpr int kEarlyTable[] = {
    0, 3, 8, 11, 12, 15, 27, 30, 30, 33, 68, 96, 100, 109,
    154, 169, 169, 173, 188, 238, 243, 291, 296, 339, 372,
    411, 465, 499, 573, 574, 581, 598, 655, 685, 831, 832
  };

  // bez[] / bezF[] slot indices (verbatim from AW kWoodRoom.h enum)
  enum BezSlot
  {
    bez_AL = 0,
    bez_AR,
    bez_BL,
    bez_BR,
    bez_CL,
    bez_CR,
    bez_SampL,
    bez_SampR,
    bez_IIRL,
    bez_IIRR,
    bez_cycle,
    bez_total
  };

  class KWoodRoom : public od::Object
  {
  public:
    KWoodRoom()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mRegen);
      addParameter(mTime);
      addParameter(mTone);
      addParameter(mReflect);
      addParameter(mPosition);
      addParameter(mMix);

      // Zero every delay-line array. One memset per array,
      // instead of the original AW source's 70+ explicit
      // per-element loops.
      memset(a3AL, 0, sizeof(a3AL)); memset(a3AR, 0, sizeof(a3AR));
      memset(a3BL, 0, sizeof(a3BL)); memset(a3BR, 0, sizeof(a3BR));
      memset(a3CL, 0, sizeof(a3CL)); memset(a3CR, 0, sizeof(a3CR));
      memset(a3DL, 0, sizeof(a3DL)); memset(a3DR, 0, sizeof(a3DR));
      memset(a3EL, 0, sizeof(a3EL)); memset(a3ER, 0, sizeof(a3ER));
      memset(a3FL, 0, sizeof(a3FL)); memset(a3FR, 0, sizeof(a3FR));
      memset(a3GL, 0, sizeof(a3GL)); memset(a3GR, 0, sizeof(a3GR));
      memset(a3HL, 0, sizeof(a3HL)); memset(a3HR, 0, sizeof(a3HR));
      memset(a3IL, 0, sizeof(a3IL)); memset(a3IR, 0, sizeof(a3IR));

      memset(a6AL, 0, sizeof(a6AL)); memset(a6AR, 0, sizeof(a6AR));
      memset(a6BL, 0, sizeof(a6BL)); memset(a6BR, 0, sizeof(a6BR));
      memset(a6CL, 0, sizeof(a6CL)); memset(a6CR, 0, sizeof(a6CR));
      memset(a6DL, 0, sizeof(a6DL)); memset(a6DR, 0, sizeof(a6DR));
      memset(a6EL, 0, sizeof(a6EL)); memset(a6ER, 0, sizeof(a6ER));
      memset(a6FL, 0, sizeof(a6FL)); memset(a6FR, 0, sizeof(a6FR));
      memset(a6GL, 0, sizeof(a6GL)); memset(a6GR, 0, sizeof(a6GR));
      memset(a6HL, 0, sizeof(a6HL)); memset(a6HR, 0, sizeof(a6HR));
      memset(a6IL, 0, sizeof(a6IL)); memset(a6IR, 0, sizeof(a6IR));
      memset(a6JL, 0, sizeof(a6JL)); memset(a6JR, 0, sizeof(a6JR));
      memset(a6KL, 0, sizeof(a6KL)); memset(a6KR, 0, sizeof(a6KR));
      memset(a6LL, 0, sizeof(a6LL)); memset(a6LR, 0, sizeof(a6LR));
      memset(a6ML, 0, sizeof(a6ML)); memset(a6MR, 0, sizeof(a6MR));
      memset(a6NL, 0, sizeof(a6NL)); memset(a6NR, 0, sizeof(a6NR));
      memset(a6OL, 0, sizeof(a6OL)); memset(a6OR, 0, sizeof(a6OR));
      memset(a6PL, 0, sizeof(a6PL)); memset(a6PR, 0, sizeof(a6PR));
      memset(a6QL, 0, sizeof(a6QL)); memset(a6QR, 0, sizeof(a6QR));
      memset(a6RL, 0, sizeof(a6RL)); memset(a6RR, 0, sizeof(a6RR));
      memset(a6SL, 0, sizeof(a6SL)); memset(a6SR, 0, sizeof(a6SR));
      memset(a6TL, 0, sizeof(a6TL)); memset(a6TR, 0, sizeof(a6TR));
      memset(a6UL, 0, sizeof(a6UL)); memset(a6UR, 0, sizeof(a6UR));
      memset(a6VL, 0, sizeof(a6VL)); memset(a6VR, 0, sizeof(a6VR));
      memset(a6WL, 0, sizeof(a6WL)); memset(a6WR, 0, sizeof(a6WR));
      memset(a6XL, 0, sizeof(a6XL)); memset(a6XR, 0, sizeof(a6XR));
      memset(a6YL, 0, sizeof(a6YL)); memset(a6YR, 0, sizeof(a6YR));
      memset(a6ZAL, 0, sizeof(a6ZAL)); memset(a6ZAR, 0, sizeof(a6ZAR));
      memset(a6ZBL, 0, sizeof(a6ZBL)); memset(a6ZBR, 0, sizeof(a6ZBR));
      memset(a6ZCL, 0, sizeof(a6ZCL)); memset(a6ZCR, 0, sizeof(a6ZCR));
      memset(a6ZDL, 0, sizeof(a6ZDL)); memset(a6ZDR, 0, sizeof(a6ZDR));
      memset(a6ZEL, 0, sizeof(a6ZEL)); memset(a6ZER, 0, sizeof(a6ZER));
      memset(a6ZFL, 0, sizeof(a6ZFL)); memset(a6ZFR, 0, sizeof(a6ZFR));
      memset(a6ZGL, 0, sizeof(a6ZGL)); memset(a6ZGR, 0, sizeof(a6ZGR));
      memset(a6ZHL, 0, sizeof(a6ZHL)); memset(a6ZHR, 0, sizeof(a6ZHR));
      memset(a6ZIL, 0, sizeof(a6ZIL)); memset(a6ZIR, 0, sizeof(a6ZIR));
      memset(a6ZJL, 0, sizeof(a6ZJL)); memset(a6ZJR, 0, sizeof(a6ZJR));
      memset(a6ZKL, 0, sizeof(a6ZKL)); memset(a6ZKR, 0, sizeof(a6ZKR));

      // Counters start at 1 (per AW source). The read formula
      //   arr[c - ((c > d) ? d+1 : 0)]
      // returns arr[0] initially, safe.
      c3AL = c3BL = c3CL = c3DL = c3EL = c3FL = c3GL = c3HL = c3IL = 1;
      c3AR = c3BR = c3CR = c3DR = c3ER = c3FR = c3GR = c3HR = c3IR = 1;

      c6AL = c6BL = c6CL = c6DL = c6EL = c6FL = c6GL = c6HL = c6IL = 1;
      c6JL = c6KL = c6LL = c6ML = c6NL = c6OL = c6PL = c6QL = c6RL = 1;
      c6SL = c6TL = c6UL = c6VL = c6WL = c6XL = c6YL = c6ZAL = c6ZBL = 1;
      c6ZCL = c6ZDL = c6ZEL = c6ZFL = c6ZGL = c6ZHL = c6ZIL = c6ZJL = c6ZKL = 1;
      c6AR = c6BR = c6CR = c6DR = c6ER = c6FR = c6GR = c6HR = c6IR = 1;
      c6JR = c6KR = c6LR = c6MR = c6NR = c6OR = c6PR = c6QR = c6RR = 1;
      c6SR = c6TR = c6UR = c6VR = c6WR = c6XR = c6YR = c6ZAR = c6ZBR = 1;
      c6ZCR = c6ZDR = c6ZER = c6ZFR = c6ZGR = c6ZHR = c6ZIR = c6ZJR = c6ZKR = 1;

      f6AL = f6BL = f6CL = f6DL = f6EL = f6FL = 0.0;
      f6FR = f6LR = f6RR = f6XR = f6ZER = f6ZKR = 0.0;
      avg6L = avg6R = 0.0;

      for (int x = 0; x < bez_total; x++)
      {
        bez[x] = 0.0;
        bezF[x] = 0.0;
      }
      // LOAD-BEARING: forces the first reverb cycle to fire so
      // first output is non-zero. Do NOT zero these out.
      bez[bez_cycle] = 1.0;
      bezF[bez_cycle] = 1.0;
    }

    virtual ~KWoodRoom() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};

    // 6 user parameters. Defaults match AW pinParameter defaults.
    od::Parameter mRegen{"Regen", 0.5f};
    od::Parameter mTime{"Time", 0.5f};        // AW "Derez"
    od::Parameter mTone{"Tone", 0.25f};       // AW "Filter"
    od::Parameter mReflect{"Reflect", 0.5f};  // AW "EarlyRF"
    od::Parameter mPosition{"Position", 0.75f};
    od::Parameter mMix{"Mix", 0.5f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();
      const int sampleFramesIn = FRAMELENGTH;

      const float A = mRegen.value();
      const float B = mTime.value();
      const float C = mTone.value();
      const float D = mReflect.value();
      const float E = mPosition.value();
      const float F = mMix.value();

      double overallscale = 1.0;
      overallscale /= 44100.0;
      overallscale *= (double)globalConfig.sampleRate;

      double fdb6ck = (0.0009765625 + 0.0009765625 + 0.001953125) * 0.3333333;
      double reg6n = (1.0 - pow(1.0 - A, 1.618033988749894)) * fdb6ck;

      double derez = B * 2.0;
      bool stepped = true;
      if (derez > 1.0)
      {
        stepped = false;
        derez = 1.0 - (derez - 1.0);
      }
      derez = fmin(fmax(derez / overallscale, 0.0005), 1.0);
      int bezFraction = (int)(1.0 / derez);
      double bezTrim = (double)bezFraction / (bezFraction + 1.0);
      if (stepped)
      {
        derez = 1.0 / bezFraction;
        bezTrim = 1.0 - (derez * bezTrim);
      }
      else
      {
        derez /= (2.0 / pow(overallscale, 0.5 - ((overallscale - 1.0) * 0.0375)));
        bezTrim = 1.0 - pow(derez * 0.5, 1.0 / (derez * 0.5));
      }

      double derezFreq = C * 2.0;
      bool steppedFreq = true;
      if (derezFreq > 1.0)
      {
        steppedFreq = false;
        derezFreq = 1.0 - (derezFreq - 1.0);
      }
      derezFreq = fmin(fmax(derezFreq, 0.0005), 1.0);
      int bezFreqFraction = (int)(1.0 / derezFreq);
      double bezFreqTrim = (double)bezFreqFraction / (bezFreqFraction + 1.0);
      if (steppedFreq)
      {
        derezFreq = 1.0 / bezFreqFraction;
        bezFreqTrim = 1.0 - (derezFreq * bezFreqTrim);
      }
      else
      {
        bezFreqTrim = 1.0 - pow(derezFreq * 0.5, 1.0 / (derezFreq * 0.5));
      }

      double earlyLoudness = pow(D, 2.0);
      int start = (int)(E * 27.0);
      int ld3G = kEarlyTable[start];
      int ld3H = kEarlyTable[start + 1];
      int ld3D = kEarlyTable[start + 2];
      int ld3A = kEarlyTable[start + 3];
      int ld3E = kEarlyTable[start + 4];
      int ld3I = kEarlyTable[start + 5];
      int ld3F = kEarlyTable[start + 6];
      int ld3B = kEarlyTable[start + 7];
      int ld3C = kEarlyTable[start + 8];
      double wet = F;

      int sampleFrames = sampleFramesIn;
      while (--sampleFrames >= 0)
      {
        double inputSampleL = *in1;
        double inputSampleR = *in2;
        // Denormal flush -- deterministic constant (dither dropped).
        if (fabs(inputSampleL) < 1.18e-23) inputSampleL = 1.18e-17;
        if (fabs(inputSampleR) < 1.18e-23) inputSampleR = 1.18e-17;
        double drySampleL = inputSampleL;
        double drySampleR = inputSampleR;

        bez[bez_cycle] += derez;
        bez[bez_SampL] += (inputSampleL * derez);
        bez[bez_SampR] += (inputSampleR * derez);

        if (bez[bez_cycle] > 1.0)
        {
          if (stepped) bez[bez_cycle] = 0.0;
          else bez[bez_cycle] -= 1.0;

          inputSampleL = bez[bez_SampL];
          inputSampleR = bez[bez_SampR];

          // ----- 3x3 trellis: layer 1 (input fanout) -----
          a3AL[c3AL] = inputSampleL;
          a3BL[c3BL] = inputSampleL;
          a3CL[c3CL] = inputSampleL;
          a3CR[c3CR] = inputSampleR;
          a3FR[c3FR] = inputSampleR;
          a3IR[c3IR] = inputSampleR;

          c3AL++; if (c3AL > ld3A) c3AL = 0;
          c3BL++; if (c3BL > ld3B) c3BL = 0;
          c3CL++; if (c3CL > ld3C) c3CL = 0;
          c3CR++; if (c3CR > ld3C) c3CR = 0;
          c3FR++; if (c3FR > ld3F) c3FR = 0;
          c3IR++; if (c3IR > ld3I) c3IR = 0;

          double hA = a3AL[c3AL - ((c3AL > ld3A) ? c3AL + 1 : 0)];
          double hB = a3BL[c3BL - ((c3BL > ld3B) ? c3BL + 1 : 0)];
          double hC = a3CL[c3CL - ((c3CL > ld3C) ? c3CL + 1 : 0)];
          double hD = a3CR[c3CR - ((c3CR > ld3C) ? c3CR + 1 : 0)];
          double hE = a3FR[c3FR - ((c3FR > ld3F) ? c3FR + 1 : 0)];
          double hF = a3IR[c3IR - ((c3IR > ld3I) ? c3IR + 1 : 0)];

          // ----- 3x3 trellis: layer 2 (diff-Householder reduction) -----
          a3DL[c3DL] = (((hB + hC) * -2.0) + hA);
          a3EL[c3EL] = (((hA + hC) * -2.0) + hB);
          a3FL[c3FL] = (((hA + hB) * -2.0) + hC);
          a3BR[c3BR] = (((hE + hF) * -2.0) + hD);
          a3ER[c3ER] = (((hD + hF) * -2.0) + hE);
          a3HR[c3HR] = (((hD + hE) * -2.0) + hF);

          c3DL++; if (c3DL > ld3D) c3DL = 0;
          c3EL++; if (c3EL > ld3E) c3EL = 0;
          c3FL++; if (c3FL > ld3F) c3FL = 0;
          c3BR++; if (c3BR > ld3B) c3BR = 0;
          c3ER++; if (c3ER > ld3E) c3ER = 0;
          c3HR++; if (c3HR > ld3H) c3HR = 0;

          hA = a3DL[c3DL - ((c3DL > ld3D) ? c3DL + 1 : 0)];
          hB = a3EL[c3EL - ((c3EL > ld3E) ? c3EL + 1 : 0)];
          hC = a3FL[c3FL - ((c3FL > ld3F) ? c3FL + 1 : 0)];
          hD = a3BR[c3BR - ((c3BR > ld3B) ? c3BR + 1 : 0)];
          hE = a3ER[c3ER - ((c3ER > ld3E) ? c3ER + 1 : 0)];
          hF = a3HR[c3HR - ((c3HR > ld3H) ? c3HR + 1 : 0)];

          // ----- 3x3 trellis: layer 3 (final reduction) -----
          a3GL[c3GL] = (((hB + hC) * -2.0) + hA);
          a3HL[c3HL] = (((hA + hC) * -2.0) + hB);
          a3IL[c3IL] = (((hA + hB) * -2.0) + hC);
          a3AR[c3AR] = (((hE + hF) * -2.0) + hD);
          a3DR[c3DR] = (((hD + hF) * -2.0) + hE);
          a3GR[c3GR] = (((hD + hE) * -2.0) + hF);

          c3GL++; if (c3GL > ld3G) c3GL = 0;
          c3HL++; if (c3HL > ld3H) c3HL = 0;
          c3IL++; if (c3IL > ld3I) c3IL = 0;
          c3AR++; if (c3AR > ld3A) c3AR = 0;
          c3DR++; if (c3DR > ld3D) c3DR = 0;
          c3GR++; if (c3GR > ld3G) c3GR = 0;

          hA = a3GL[c3GL - ((c3GL > ld3G) ? c3GL + 1 : 0)];
          hB = a3HL[c3HL - ((c3HL > ld3H) ? c3HL + 1 : 0)];
          hC = a3IL[c3IL - ((c3IL > ld3I) ? c3IL + 1 : 0)];
          hD = a3AR[c3AR - ((c3AR > ld3A) ? c3AR + 1 : 0)];
          hE = a3DR[c3DR - ((c3DR > ld3D) ? c3DR + 1 : 0)];
          hF = a3GR[c3GR - ((c3GR > ld3G) ? c3GR + 1 : 0)];

          double earlyReflectionL = (((hB + hC) * -2.0) + hA) * -0.0625;
          double earlyReflectionR = (((hE + hF) * -2.0) + hD) * -0.0625;

          inputSampleL -= earlyReflectionL;
          inputSampleR -= earlyReflectionR;

          // ----- 6x6 trellis: input fanout + feedback injection -----
          a6AL[c6AL] = inputSampleL + (f6BL * reg6n);
          a6BL[c6BL] = inputSampleL + (f6CL * reg6n);
          a6CL[c6CL] = inputSampleL + (f6DL * reg6n);
          a6DL[c6DL] = inputSampleL + (f6EL * reg6n);
          a6EL[c6EL] = inputSampleL + (f6FL * reg6n);
          a6FL[c6FL] = inputSampleL + (f6AL * reg6n);

          a6FR[c6FR] = inputSampleR + (f6LR * reg6n);
          a6LR[c6LR] = inputSampleR + (f6RR * reg6n);
          a6RR[c6RR] = inputSampleR + (f6XR * reg6n);
          a6XR[c6XR] = inputSampleR + (f6ZER * reg6n);
          a6ZER[c6ZER] = inputSampleR + (f6ZKR * reg6n);
          a6ZKR[c6ZKR] = inputSampleR + (f6FR * reg6n);

          // ===== Left side 6x6 trellis (6 layers) =====

          // Layer 1
          c6AL++; if (c6AL > d6A) c6AL = 0;
          c6BL++; if (c6BL > d6B) c6BL = 0;
          c6CL++; if (c6CL > d6C) c6CL = 0;
          c6DL++; if (c6DL > d6D) c6DL = 0;
          c6EL++; if (c6EL > d6E) c6EL = 0;
          c6FL++; if (c6FL > d6F) c6FL = 0;

          hA = a6AL[c6AL - ((c6AL > d6A) ? d6A + 1 : 0)];
          hB = a6BL[c6BL - ((c6BL > d6B) ? d6B + 1 : 0)];
          hC = a6CL[c6CL - ((c6CL > d6C) ? d6C + 1 : 0)];
          hD = a6DL[c6DL - ((c6DL > d6D) ? d6D + 1 : 0)];
          hE = a6EL[c6EL - ((c6EL > d6E) ? d6E + 1 : 0)];
          hF = a6FL[c6FL - ((c6FL > d6F) ? d6F + 1 : 0)];

          a6GL[c6GL] = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          a6HL[c6HL] = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          a6IL[c6IL] = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          a6JL[c6JL] = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          a6KL[c6KL] = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          a6LL[c6LL] = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          // Layer 2
          c6GL++; if (c6GL > d6G) c6GL = 0;
          c6HL++; if (c6HL > d6H) c6HL = 0;
          c6IL++; if (c6IL > d6I) c6IL = 0;
          c6JL++; if (c6JL > d6J) c6JL = 0;
          c6KL++; if (c6KL > d6K) c6KL = 0;
          c6LL++; if (c6LL > d6L) c6LL = 0;

          hA = a6GL[c6GL - ((c6GL > d6G) ? d6G + 1 : 0)];
          hB = a6HL[c6HL - ((c6HL > d6H) ? d6H + 1 : 0)];
          hC = a6IL[c6IL - ((c6IL > d6I) ? d6I + 1 : 0)];
          hD = a6JL[c6JL - ((c6JL > d6J) ? d6J + 1 : 0)];
          hE = a6KL[c6KL - ((c6KL > d6K) ? d6K + 1 : 0)];
          hF = a6LL[c6LL - ((c6LL > d6L) ? d6L + 1 : 0)];

          a6ML[c6ML] = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          a6NL[c6NL] = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          a6OL[c6OL] = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          a6PL[c6PL] = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          a6QL[c6QL] = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          a6RL[c6RL] = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          // Layer 3
          c6ML++; if (c6ML > d6M) c6ML = 0;
          c6NL++; if (c6NL > d6N) c6NL = 0;
          c6OL++; if (c6OL > d6O) c6OL = 0;
          c6PL++; if (c6PL > d6P) c6PL = 0;
          c6QL++; if (c6QL > d6Q) c6QL = 0;
          c6RL++; if (c6RL > d6R) c6RL = 0;

          hA = a6ML[c6ML - ((c6ML > d6M) ? d6M + 1 : 0)];
          hB = a6NL[c6NL - ((c6NL > d6N) ? d6N + 1 : 0)];
          hC = a6OL[c6OL - ((c6OL > d6O) ? d6O + 1 : 0)];
          hD = a6PL[c6PL - ((c6PL > d6P) ? d6P + 1 : 0)];
          hE = a6QL[c6QL - ((c6QL > d6Q) ? d6Q + 1 : 0)];
          hF = a6RL[c6RL - ((c6RL > d6R) ? d6R + 1 : 0)];

          a6SL[c6SL] = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          a6TL[c6TL] = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          a6UL[c6UL] = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          a6VL[c6VL] = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          a6WL[c6WL] = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          a6XL[c6XL] = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          // Layer 4
          c6SL++; if (c6SL > d6S) c6SL = 0;
          c6TL++; if (c6TL > d6T) c6TL = 0;
          c6UL++; if (c6UL > d6U) c6UL = 0;
          c6VL++; if (c6VL > d6V) c6VL = 0;
          c6WL++; if (c6WL > d6W) c6WL = 0;
          c6XL++; if (c6XL > d6X) c6XL = 0;

          hA = a6SL[c6SL - ((c6SL > d6S) ? d6S + 1 : 0)];
          hB = a6TL[c6TL - ((c6TL > d6T) ? d6T + 1 : 0)];
          hC = a6UL[c6UL - ((c6UL > d6U) ? d6U + 1 : 0)];
          hD = a6VL[c6VL - ((c6VL > d6V) ? d6V + 1 : 0)];
          hE = a6WL[c6WL - ((c6WL > d6W) ? d6W + 1 : 0)];
          hF = a6XL[c6XL - ((c6XL > d6X) ? d6X + 1 : 0)];

          a6YL[c6YL] = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          a6ZAL[c6ZAL] = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          a6ZBL[c6ZBL] = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          a6ZCL[c6ZCL] = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          a6ZDL[c6ZDL] = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          a6ZEL[c6ZEL] = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          // Layer 5
          c6YL++; if (c6YL > d6Y) c6YL = 0;
          c6ZAL++; if (c6ZAL > d6ZA) c6ZAL = 0;
          c6ZBL++; if (c6ZBL > d6ZB) c6ZBL = 0;
          c6ZCL++; if (c6ZCL > d6ZC) c6ZCL = 0;
          c6ZDL++; if (c6ZDL > d6ZD) c6ZDL = 0;
          c6ZEL++; if (c6ZEL > d6ZE) c6ZEL = 0;

          hA = a6YL[c6YL - ((c6YL > d6Y) ? d6Y + 1 : 0)];
          hB = a6ZAL[c6ZAL - ((c6ZAL > d6ZA) ? d6ZA + 1 : 0)];
          hC = a6ZBL[c6ZBL - ((c6ZBL > d6ZB) ? d6ZB + 1 : 0)];
          hD = a6ZCL[c6ZCL - ((c6ZCL > d6ZC) ? d6ZC + 1 : 0)];
          hE = a6ZDL[c6ZDL - ((c6ZDL > d6ZD) ? d6ZD + 1 : 0)];
          hF = a6ZEL[c6ZEL - ((c6ZEL > d6ZE) ? d6ZE + 1 : 0)];

          a6ZFL[c6ZFL] = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          a6ZGL[c6ZGL] = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          a6ZHL[c6ZHL] = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          a6ZIL[c6ZIL] = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          a6ZJL[c6ZJL] = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          a6ZKL[c6ZKL] = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          // Layer 6 -- output goes to R-side feedback taps
          c6ZFL++; if (c6ZFL > d6ZF) c6ZFL = 0;
          c6ZGL++; if (c6ZGL > d6ZG) c6ZGL = 0;
          c6ZHL++; if (c6ZHL > d6ZH) c6ZHL = 0;
          c6ZIL++; if (c6ZIL > d6ZI) c6ZIL = 0;
          c6ZJL++; if (c6ZJL > d6ZJ) c6ZJL = 0;
          c6ZKL++; if (c6ZKL > d6ZK) c6ZKL = 0;

          hA = a6ZFL[c6ZFL - ((c6ZFL > d6ZF) ? d6ZF + 1 : 0)];
          hB = a6ZGL[c6ZGL - ((c6ZGL > d6ZG) ? d6ZG + 1 : 0)];
          hC = a6ZHL[c6ZHL - ((c6ZHL > d6ZH) ? d6ZH + 1 : 0)];
          hD = a6ZIL[c6ZIL - ((c6ZIL > d6ZI) ? d6ZI + 1 : 0)];
          hE = a6ZJL[c6ZJL - ((c6ZJL > d6ZJ) ? d6ZJ + 1 : 0)];
          hF = a6ZKL[c6ZKL - ((c6ZKL > d6ZK) ? d6ZK + 1 : 0)];

          f6FR = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          f6LR = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          f6RR = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          f6XR = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          f6ZER = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          f6ZKR = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          inputSampleL = ((hA * 2.0) - (hB + hC + hD + hE + hF)) * 0.001953125;

          // ===== Right side 6x6 trellis (6 layers) =====

          // Layer 1
          c6FR++; if (c6FR > d6F) c6FR = 0;
          c6LR++; if (c6LR > d6L) c6LR = 0;
          c6RR++; if (c6RR > d6R) c6RR = 0;
          c6XR++; if (c6XR > d6X) c6XR = 0;
          c6ZER++; if (c6ZER > d6ZE) c6ZER = 0;
          c6ZKR++; if (c6ZKR > d6ZK) c6ZKR = 0;

          hA = a6FR[c6FR - ((c6FR > d6F) ? d6F + 1 : 0)];
          hB = a6LR[c6LR - ((c6LR > d6L) ? d6L + 1 : 0)];
          hC = a6RR[c6RR - ((c6RR > d6R) ? d6R + 1 : 0)];
          hD = a6XR[c6XR - ((c6XR > d6X) ? d6X + 1 : 0)];
          hE = a6ZER[c6ZER - ((c6ZER > d6ZE) ? d6ZE + 1 : 0)];
          hF = a6ZKR[c6ZKR - ((c6ZKR > d6ZK) ? d6ZK + 1 : 0)];

          a6ER[c6ER] = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          a6KR[c6KR] = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          a6QR[c6QR] = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          a6WR[c6WR] = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          a6ZDR[c6ZDR] = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          a6ZJR[c6ZJR] = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          // Layer 2
          c6ER++; if (c6ER > d6E) c6ER = 0;
          c6KR++; if (c6KR > d6K) c6KR = 0;
          c6QR++; if (c6QR > d6Q) c6QR = 0;
          c6WR++; if (c6WR > d6W) c6WR = 0;
          c6ZDR++; if (c6ZDR > d6ZD) c6ZDR = 0;
          c6ZJR++; if (c6ZJR > d6ZJ) c6ZJR = 0;

          hA = a6ER[c6ER - ((c6ER > d6E) ? d6E + 1 : 0)];
          hB = a6KR[c6KR - ((c6KR > d6K) ? d6K + 1 : 0)];
          hC = a6QR[c6QR - ((c6QR > d6Q) ? d6Q + 1 : 0)];
          hD = a6WR[c6WR - ((c6WR > d6W) ? d6W + 1 : 0)];
          hE = a6ZDR[c6ZDR - ((c6ZDR > d6ZD) ? d6ZD + 1 : 0)];
          hF = a6ZJR[c6ZJR - ((c6ZJR > d6ZJ) ? d6ZJ + 1 : 0)];

          a6DR[c6DR] = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          a6JR[c6JR] = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          a6PR[c6PR] = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          a6VR[c6VR] = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          a6ZCR[c6ZCR] = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          a6ZIR[c6ZIR] = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          // Layer 3
          c6DR++; if (c6DR > d6D) c6DR = 0;
          c6JR++; if (c6JR > d6J) c6JR = 0;
          c6PR++; if (c6PR > d6P) c6PR = 0;
          c6VR++; if (c6VR > d6V) c6VR = 0;
          c6ZCR++; if (c6ZCR > d6ZC) c6ZCR = 0;
          c6ZIR++; if (c6ZIR > d6ZI) c6ZIR = 0;

          hA = a6DR[c6DR - ((c6DR > d6D) ? d6D + 1 : 0)];
          hB = a6JR[c6JR - ((c6JR > d6J) ? d6J + 1 : 0)];
          hC = a6PR[c6PR - ((c6PR > d6P) ? d6P + 1 : 0)];
          hD = a6VR[c6VR - ((c6VR > d6V) ? d6V + 1 : 0)];
          hE = a6ZCR[c6ZCR - ((c6ZCR > d6ZC) ? d6ZC + 1 : 0)];
          hF = a6ZIR[c6ZIR - ((c6ZIR > d6ZI) ? d6ZI + 1 : 0)];

          a6CR[c6CR] = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          a6IR[c6IR] = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          a6OR[c6OR] = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          a6UR[c6UR] = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          a6ZBR[c6ZBR] = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          a6ZHR[c6ZHR] = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          // Layer 4
          c6CR++; if (c6CR > d6C) c6CR = 0;
          c6IR++; if (c6IR > d6I) c6IR = 0;
          c6OR++; if (c6OR > d6O) c6OR = 0;
          c6UR++; if (c6UR > d6U) c6UR = 0;
          c6ZBR++; if (c6ZBR > d6ZB) c6ZBR = 0;
          c6ZHR++; if (c6ZHR > d6ZH) c6ZHR = 0;

          hA = a6CR[c6CR - ((c6CR > d6C) ? d6C + 1 : 0)];
          hB = a6IR[c6IR - ((c6IR > d6I) ? d6I + 1 : 0)];
          hC = a6OR[c6OR - ((c6OR > d6O) ? d6O + 1 : 0)];
          hD = a6UR[c6UR - ((c6UR > d6U) ? d6U + 1 : 0)];
          hE = a6ZBR[c6ZBR - ((c6ZBR > d6ZB) ? d6ZB + 1 : 0)];
          hF = a6ZHR[c6ZHR - ((c6ZHR > d6ZH) ? d6ZH + 1 : 0)];

          a6BR[c6BR] = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          a6HR[c6HR] = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          a6NR[c6NR] = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          a6TR[c6TR] = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          a6ZAR[c6ZAR] = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          a6ZGR[c6ZGR] = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          // Layer 5 -- source has c6ZBR++ instead of c6ER++;
          // preserved literally per feedback_identical_means_identical.
          c6BR++; if (c6BR > d6B) c6BR = 0;
          c6HR++; if (c6HR > d6H) c6HR = 0;
          c6NR++; if (c6NR > d6N) c6NR = 0;
          c6TR++; if (c6TR > d6T) c6TR = 0;
          c6ZBR++; if (c6ZBR > d6ZB) c6ZBR = 0;
          c6ZGR++; if (c6ZGR > d6ZG) c6ZGR = 0;

          hA = a6BR[c6BR - ((c6BR > d6B) ? d6B + 1 : 0)];
          hB = a6HR[c6HR - ((c6HR > d6H) ? d6H + 1 : 0)];
          hC = a6NR[c6NR - ((c6NR > d6N) ? d6N + 1 : 0)];
          hD = a6TR[c6TR - ((c6TR > d6T) ? d6T + 1 : 0)];
          hE = a6ZAR[c6ZAR - ((c6ZAR > d6ZA) ? d6ZA + 1 : 0)];
          hF = a6ZGR[c6ZGR - ((c6ZGR > d6ZG) ? d6ZG + 1 : 0)];

          a6AR[c6AR] = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          a6GR[c6GR] = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          a6MR[c6MR] = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          a6SR[c6SR] = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          a6YR[c6YR] = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          a6ZFR[c6ZFR] = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          // Layer 6 -- output goes to L-side feedback taps
          c6AR++; if (c6AR > d6A) c6AR = 0;
          c6GR++; if (c6GR > d6G) c6GR = 0;
          c6MR++; if (c6MR > d6M) c6MR = 0;
          c6SR++; if (c6SR > d6S) c6SR = 0;
          c6YR++; if (c6YR > d6Y) c6YR = 0;
          c6ZFR++; if (c6ZFR > d6ZF) c6ZFR = 0;

          hA = a6AR[c6AR - ((c6AR > d6A) ? d6A + 1 : 0)];
          hB = a6GR[c6GR - ((c6GR > d6G) ? d6G + 1 : 0)];
          hC = a6MR[c6MR - ((c6MR > d6M) ? d6M + 1 : 0)];
          hD = a6SR[c6SR - ((c6SR > d6S) ? d6S + 1 : 0)];
          hE = a6YR[c6YR - ((c6YR > d6Y) ? d6Y + 1 : 0)];
          hF = a6ZFR[c6ZFR - ((c6ZFR > d6ZF) ? d6ZF + 1 : 0)];

          f6AL = ((hA * 2.0) - (hB + hC + hD + hE + hF));
          f6BL = ((hB * 2.0) - (hA + hC + hD + hE + hF));
          f6CL = ((hC * 2.0) - (hA + hB + hD + hE + hF));
          f6DL = ((hD * 2.0) - (hA + hB + hC + hE + hF));
          f6EL = ((hE * 2.0) - (hA + hB + hC + hD + hF));
          f6FL = ((hF * 2.0) - (hA + hB + hC + hD + hE));

          inputSampleR = ((hA * 2.0) - (hB + hC + hD + hE + hF)) * 0.001953125;

          // ===== Inner Bezier (bezF) + IIR filter stage =====
          bezF[bez_cycle] += derezFreq;
          bezF[bez_SampL] += (inputSampleL * derezFreq);
          bezF[bez_SampR] += (inputSampleR * derezFreq);
          if (bezF[bez_cycle] > 1.0)
          {
            if (steppedFreq) bezF[bez_cycle] = 0.0;
            else bezF[bez_cycle] -= 1.0;
            bezF[bez_CL] = bezF[bez_BL];
            bezF[bez_BL] = bezF[bez_AL];
            bezF[bez_AL] = bezF[bez_SampL];
            bezF[bez_SampL] = 0.0;
            bezF[bez_CR] = bezF[bez_BR];
            bezF[bez_BR] = bezF[bez_AR];
            bezF[bez_AR] = bezF[bez_SampR];
            bezF[bez_SampR] = 0.0;
          }
          double Xf = bezF[bez_cycle] * bezFreqTrim;
          double CBLfreq = (bezF[bez_CL] * (1.0 - Xf)) + (bezF[bez_BL] * Xf);
          double BALfreq = (bezF[bez_BL] * (1.0 - Xf)) + (bezF[bez_AL] * Xf);
          inputSampleL = (bezF[bez_BL] + (CBLfreq * (1.0 - Xf)) + (BALfreq * Xf)) * 0.125;
          double CBRfreq = (bezF[bez_CR] * (1.0 - Xf)) + (bezF[bez_BR] * Xf);
          double BARfreq = (bezF[bez_BR] * (1.0 - Xf)) + (bezF[bez_AR] * Xf);
          inputSampleR = (bezF[bez_BR] + (CBRfreq * (1.0 - Xf)) + (BARfreq * Xf)) * 0.125;

          inputSampleL = bezF[bez_IIRL] = (inputSampleL * derezFreq) + (bezF[bez_IIRL] * (1.0 - derezFreq));
          inputSampleR = bezF[bez_IIRR] = (inputSampleR * derezFreq) + (bezF[bez_IIRR] * (1.0 - derezFreq));

          inputSampleL += (earlyReflectionL * earlyLoudness);
          inputSampleR += (earlyReflectionR * earlyLoudness);

          // Shift the outer-Bezier ABC history
          bez[bez_CL] = bez[bez_BL];
          bez[bez_BL] = bez[bez_AL];
          bez[bez_AL] = inputSampleL;
          bez[bez_SampL] = 0.0;

          bez[bez_CR] = bez[bez_BR];
          bez[bez_BR] = bez[bez_AR];
          bez[bez_AR] = inputSampleR;
          bez[bez_SampR] = 0.0;
        }
        // ----- Outer Bezier reconstruction (every input sample) -----
        double X = bez[bez_cycle] * bezTrim;
        double CBL = (bez[bez_CL] * (1.0 - X)) + (bez[bez_BL] * X);
        double CBR = (bez[bez_CR] * (1.0 - X)) + (bez[bez_BR] * X);
        double BAL = (bez[bez_BL] * (1.0 - X)) + (bez[bez_AL] * X);
        double BAR = (bez[bez_BR] * (1.0 - X)) + (bez[bez_AR] * X);
        inputSampleL = (bez[bez_BL] + (CBL * (1.0 - X)) + (BAL * X)) * -0.25;
        inputSampleR = (bez[bez_BR] + (CBR * (1.0 - X)) + (BAR * X)) * -0.25;

        inputSampleL = bez[bez_IIRL] = (inputSampleL * derez) + (bez[bez_IIRL] * (1.0 - derez));
        inputSampleR = bez[bez_IIRR] = (inputSampleR * derez) + (bez[bez_IIRR] * (1.0 - derez));

        // Wet/dry
        inputSampleL = (inputSampleL * wet) + (drySampleL * (1.0 - wet));
        inputSampleR = (inputSampleR * wet) + (drySampleR * (1.0 - wet));

        *out1 = (float)inputSampleL;
        *out2 = (float)inputSampleR;

        in1++;
        in2++;
        out1++;
        out2++;
      }
    }

  private:
    // 3x3 trellis (early reflections), per side
    double a3AL[d3A + 5], a3AR[d3A + 5];
    double a3BL[d3B + 5], a3BR[d3B + 5];
    double a3CL[d3C + 5], a3CR[d3C + 5];
    double a3DL[d3D + 5], a3DR[d3D + 5];
    double a3EL[d3E + 5], a3ER[d3E + 5];
    double a3FL[d3F + 5], a3FR[d3F + 5];
    double a3GL[d3G + 5], a3GR[d3G + 5];
    double a3HL[d3H + 5], a3HR[d3H + 5];
    double a3IL[d3I + 5], a3IR[d3I + 5];
    int c3AL, c3AR, c3BL, c3BR, c3CL, c3CR;
    int c3DL, c3DR, c3EL, c3ER, c3FL, c3FR;
    int c3GL, c3GR, c3HL, c3HR, c3IL, c3IR;

    // 6x6 trellis (main reverb), per side
    double a6AL[d6A + 5], a6AR[d6A + 5];
    double a6BL[d6B + 5], a6BR[d6B + 5];
    double a6CL[d6C + 5], a6CR[d6C + 5];
    double a6DL[d6D + 5], a6DR[d6D + 5];
    double a6EL[d6E + 5], a6ER[d6E + 5];
    double a6FL[d6F + 5], a6FR[d6F + 5];
    double a6GL[d6G + 5], a6GR[d6G + 5];
    double a6HL[d6H + 5], a6HR[d6H + 5];
    double a6IL[d6I + 5], a6IR[d6I + 5];
    double a6JL[d6J + 5], a6JR[d6J + 5];
    double a6KL[d6K + 5], a6KR[d6K + 5];
    double a6LL[d6L + 5], a6LR[d6L + 5];
    double a6ML[d6M + 5], a6MR[d6M + 5];
    double a6NL[d6N + 5], a6NR[d6N + 5];
    double a6OL[d6O + 5], a6OR[d6O + 5];
    double a6PL[d6P + 5], a6PR[d6P + 5];
    double a6QL[d6Q + 5], a6QR[d6Q + 5];
    double a6RL[d6R + 5], a6RR[d6R + 5];
    double a6SL[d6S + 5], a6SR[d6S + 5];
    double a6TL[d6T + 5], a6TR[d6T + 5];
    double a6UL[d6U + 5], a6UR[d6U + 5];
    double a6VL[d6V + 5], a6VR[d6V + 5];
    double a6WL[d6W + 5], a6WR[d6W + 5];
    double a6XL[d6X + 5], a6XR[d6X + 5];
    double a6YL[d6Y + 5], a6YR[d6Y + 5];
    double a6ZAL[d6ZA + 5], a6ZAR[d6ZA + 5];
    double a6ZBL[d6ZB + 5], a6ZBR[d6ZB + 5];
    double a6ZCL[d6ZC + 5], a6ZCR[d6ZC + 5];
    double a6ZDL[d6ZD + 5], a6ZDR[d6ZD + 5];
    double a6ZEL[d6ZE + 5], a6ZER[d6ZE + 5];
    double a6ZFL[d6ZF + 5], a6ZFR[d6ZF + 5];
    double a6ZGL[d6ZG + 5], a6ZGR[d6ZG + 5];
    double a6ZHL[d6ZH + 5], a6ZHR[d6ZH + 5];
    double a6ZIL[d6ZI + 5], a6ZIR[d6ZI + 5];
    double a6ZJL[d6ZJ + 5], a6ZJR[d6ZJ + 5];
    double a6ZKL[d6ZK + 5], a6ZKR[d6ZK + 5];

    int c6AL, c6BL, c6CL, c6DL, c6EL, c6FL, c6GL, c6HL, c6IL;
    int c6JL, c6KL, c6LL, c6ML, c6NL, c6OL, c6PL, c6QL, c6RL;
    int c6SL, c6TL, c6UL, c6VL, c6WL, c6XL, c6YL, c6ZAL, c6ZBL;
    int c6ZCL, c6ZDL, c6ZEL, c6ZFL, c6ZGL, c6ZHL, c6ZIL, c6ZJL, c6ZKL;
    int c6AR, c6BR, c6CR, c6DR, c6ER, c6FR, c6GR, c6HR, c6IR;
    int c6JR, c6KR, c6LR, c6MR, c6NR, c6OR, c6PR, c6QR, c6RR;
    int c6SR, c6TR, c6UR, c6VR, c6WR, c6XR, c6YR, c6ZAR, c6ZBR;
    int c6ZCR, c6ZDR, c6ZER, c6ZFR, c6ZGR, c6ZHR, c6ZIR, c6ZJR, c6ZKR;

    double f6AL, f6BL, f6CL, f6DL, f6EL, f6FL;
    double f6FR, f6LR, f6RR, f6XR, f6ZER, f6ZKR;
    double avg6L, avg6R;

    double bez[bez_total];
    double bezF[bez_total];
#endif
  };

} // namespace house
