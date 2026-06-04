// house::KWoodRoomDSP
//
// Port of Airwindows kWoodRoom (Chris Johnson, MIT) to ER-301.
// Plain DSP helper, not an od::Object subclass — owned by the
// kWoodRoom od::Object (or by the Smoketest harness during Phase 0).
//
// Per-character work is unchanged from the AW reference, so we
// ship under the upstream name. AW unit naming convention is
// preserved across the port pipeline; recombined / original
// units use habitat-native names.
//
// This is the Phase 0 faithful port:
//   - VST host / std::set / std::string deps dropped
//   - Per-sample 32-bit dither dropped (per CloudSeed lesson —
//     pow() per sample is a hang risk on Cortex-A8; ER-301's
//     internal float bus doesn't need dither anyway)
//   - State arrays kept as `double` end-to-end (no precision
//     reduction yet; defer to Phase 1+ optimization)
//   - Constructor body uses memset for the array zeroing
//     instead of the 70+ explicit per-array loops in the original
//   - bez[bez_cycle] = 1.0 and bezF[bez_cycle] = 1.0 first-frame
//     initialization is LOAD-BEARING and preserved literally:
//     it forces the first reverb cycle to fire so the first
//     output is non-zero. Per feedback_identical_means_identical.

#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace house
{

  // Delay-line sizes (verbatim from Airwindows kWoodRoom.h).
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

  // bez[] / bezF[] slot indices (verbatim from kWoodRoom.h enum)
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

  class KWoodRoomDSP
  {
  public:
    // Public parameters. Caller sets these between blocks. Range
    // is 0..1 each (the original plugin's pinParameter clamp).
    // Plugin names → ER-301 surface (locked in port plan):
    //   A: Regen   B: Derez (Time)  C: Filter (Tone)
    //   D: EarlyRF (Reflect)  E: Position  F: Dry/Wet (Mix)
    float A;
    float B;
    float C;
    float D;
    float E;
    float F;

    KWoodRoomDSP();

    // Per-block process. inL/inR are read, outL/outR are written.
    // sampleRate is passed in (rather than read from a global) so
    // this helper is testable in isolation.
    void process(const float *inL, const float *inR,
                 float *outL, float *outR,
                 int frameLen, float sampleRate);

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

    // 6x6 feedback taps (cross-channel: L's 6 feed R, R's 6 feed L)
    double f6AL, f6BL, f6CL, f6DL, f6EL, f6FL;
    double f6FR, f6LR, f6RR, f6XR, f6ZER, f6ZKR;
    double avg6L, avg6R;

    // Bezier-undersampling state. bez = outer (input rate), bezF
    // = inner (filter rate inside the reverb cycle).
    double bez[bez_total];
    double bezF[bez_total];
  };

} // namespace house
