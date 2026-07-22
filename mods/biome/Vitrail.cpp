#include "Vitrail.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

// no-tree-vectorize is load-bearing per feedback_disable_tree_vectorize_am335x
// (TOP PRIORITY) + feedback_neon_hint_surfaces: keeps GCC from auto-vectorizing
// the per-sample loop into quad-D NEON with :64/:128 alignment hints that hard-
// fault on Cortex-A8. The package builds -O3 -ftree-vectorize -ffast-math; this
// pragma opts this file out.
#pragma GCC optimize("no-tree-vectorize")

// NOTE (am335x hardening, milestone 2): remaining hot-path levers before/after
// hardware (per planning/vitrail-unit.md, build it right then harden):
//   - replace per-sample powf (cutoffHz) with a fast exp2 approx / block-rate update
//   - hoist the block-constant `mode` switch out of the per-sample loop (branchless
//     tap select) per feedback_runtime_branched_dsp_dispatch
//   - evaluate float vs double for the SVF state (feedback_cortex_a8_no_double)
//   - confirm file-level -fno-tree-vectorize + objdump has no NEON :64/:128 hints
// DONE: tanh -> spiralFastSaturate (Taylor poly, ~20x cheaper than libm on A8;
//   feedback_spiral_feedback_governor) + soft-knee energy governor for the scream.

namespace stolmine
{

  static const double N_RATIO = 25.0;   // switched-cap clock = cutoff * N (measured)
  static const double XCOUPLE = 0.06;   // each cutoff pulls the OTHER clock down ~6%
  static const double FEEDTHRU = 0.03;  // clock feedthrough into each core
  static const double DRIFT = 0.05;     // analog clock drift depth (+/-5%)
  static const double DRIFT_SMOOTH = 0.00005; // per-sample smoothing -> ~Hz-scale wander
  static const double LOG2_166 = 7.3813971;  // log2(5000/30) - cutoff exponent scale
  static const double SAT_D = 0.6667;   // spiral saturator density -> bounds +/-1.5
  // Soft-knee energy governor: raises damping only when the resonance runs hot,
  // taming the >0.7 resonant-peak scream while leaving the moderate self-osc and
  // comb (both below the knee) untouched. See feedback_spiral_feedback_governor.
  static const double GOV_AMT = 4.0;
  static const double GOV_KNEE = 0.25;
  static const double GOV_ENV = 0.02;   // per-tick envelope smoothing on |bp|

  // mode -> shared SVF-feedback scale (MODE_Q); index = mode-1.
  // 1=LP 2=BP 3=HP 4=Notch 5=AP 6=Hidden
  static const double kModeQ[6] = {1.0, 1.0, 0.7, 1.3, 1.1, 0.9};

  static inline uint32_t lcgNext(uint32_t &s) { s = s * 1103515245u + 12345u; return s; }
  static inline double lcgBipolar(uint32_t &s)
  {
    return (double)((lcgNext(s) >> 8) & 0xFFFF) / 32767.5 - 1.0;
  }

  // Fast 2^x (polynomial mantissa + ldexp exponent) - replaces per-sample powf so
  // audio-rate cutoff FM AND V/oct transposition stay sample-accurate but cheap on
  // Cortex-A8 (no libm pow/exp per sample). Max error ~0.03% over the used range.
  static inline double fastExp2(double x)
  {
    if (x < -60.0) x = -60.0; else if (x > 60.0) x = 60.0;
    double xi = floor(x);
    double f = x - xi;
    double p = 1.0 + f * (0.6931472 + f * (0.2402265 + f * (0.0555041 + f * 0.0096181)));
    return ldexp(p, (int)xi);
  }

  // clock Hz from a [0,1] cutoff knob: 30 Hz .. 5 kHz exp (saturating past 0.8).
  static inline double clockHz(double k)
  {
    if (k < 0.0) k = 0.0; else if (k > 1.0) k = 1.0;
    double e = k / 0.8; if (e > 1.0) e = 1.0;
    return 30.0 * fastExp2(LOG2_166 * e);
  }

  // Routing type index {0=LP,1=BP,2=HP,3=AP,4=Notch} -> tapSel mode (1..5).
  static const int kTypeMode[5] = {1, 2, 3, 5, 4};

  static inline double softclip(double x)   // odd-dominant symmetric clip
  {
    if (x > 1.0) return 0.66667;
    if (x < -1.0) return -0.66667;
    return x - x * x * x * (1.0 / 3.0);
  }

  // spiralFastSaturate (inlined from house/atoms/Spiral.h): Taylor-sin saturator,
  // output bounded to +/-1/d, ~20x cheaper than libm tanh on Cortex-A8 (no DP NEON).
  static inline double spiralSat(double x, double d)
  {
    double a = fabs(x) * d;
    if (a > 1.5707963267948966) a = 1.5707963267948966;
    double x2 = a * a;
    double s = a * (1.0 + x2 * (-0.16666666666666666 + x2 * 0.008333333333333333));
    return (x > 0.0) ? (s / d) : -(s / d);
  }

  static inline double tapSel(int mode, double lp, double bp, double hp, double q)
  {
    switch (mode)
    {
    case 1: return lp;
    case 2: return bp;
    case 3: return hp;
    case 4: return lp + hp;            // notch
    case 5: return lp - q * bp + hp;   // allpass-ish
    default: return hp + 0.3 * lp;     // hidden (bright notch)
    }
  }

  // Advance one switched-cap core by one sample: the SVF state updates ONLY on a
  // clock tick (sample-and-hold + zero-order hold), so aliasing/imaging/comb fall
  // out at the clock rate and the clock feedthrough seeds ringing. `held` persists
  // between ticks (the ZOH).
  static inline double coreStep(double &ph, double inc, double &lp, double &bp,
                                double &hp, double &held, double &env, double xin,
                                double q, double g, double ft, int mode)
  {
    ph += inc;
    if (ph >= 1.0)
    {
      ph -= 1.0;
      double sq = (ph < 0.5) ? 1.0 : -1.0;
      double in2 = xin + ft * sq;
      // soft-knee energy governor: extra damping only when this core runs hot.
      double qEff = q + (env > GOV_KNEE ? (env - GOV_KNEE) * GOV_AMT : 0.0);
      hp = in2 - lp - qEff * bp;
      bp += g * hp; bp = spiralSat(bp, SAT_D);
      lp += g * bp; lp = spiralSat(lp, SAT_D);
      held = tapSel(mode, lp, bp, hp, qEff);
      env += GOV_ENV * (fabs(bp) - env);
    }
    return held;
  }

  struct Vitrail::Internal
  {
    // core A
    double phA = 0.0, lpA = 0.0, bpA = 0.0, hpA = 0.0, heldA = 0.0, envA = 0.0;
    // core B (phase offset keeps the two clocks non-degenerate)
    double phB = 0.31, lpB = 0.0, bpB = 0.0, hpB = 0.0, heldB = 0.0, envB = 0.0;
    double fbk = 0.0;        // shared resonance loop (output -> filter inputs)
    double aliasPrev = 0.0;  // alias-HI HF smoothing state
    // clock drift
    uint32_t rng = 0x1234567u;
    double driftA = 0.0, driftB = 0.0, driftTgtA = 0.0, driftTgtB = 0.0;
  };

  Vitrail::Vitrail()
  {
    addInput(mIn);
    addInput(mCutA);
    addInput(mCutB);
    addInput(mRes);
    addInput(mGain);
    addOutput(mOut);
    addParameter(mRouting);
    addParameter(mClkSrc);
    addOption(mAlias);
    mpInternal = new Internal();
  }

  Vitrail::~Vitrail()
  {
    delete mpInternal;
  }

  void Vitrail::process()
  {
    float *in = mIn.buffer();
    float *cutAb = mCutA.buffer();
    float *cutBb = mCutB.buffer();
    float *resb = mRes.buffer();
    float *gainb = mGain.buffer();
    float *out = mOut.buffer();
    Internal &I = *mpInternal;

    int routing = CLAMP(0, 49, (int)(mRouting.value() + 0.5f));
    int clk = CLAMP(0, 2, (int)(mClkSrc.value() + 0.5f));
    int alias = CLAMP(1, 2, (int)(mAlias.value() + 0.5f));

    // decode routing: [0,25) series a>b, [25,50) parallel a+b; within, idx = a*5+b.
    bool parallel = routing >= 25;
    int r = parallel ? routing - 25 : routing;
    int typeA = kTypeMode[(r / 5) % 5];
    int typeB = kTypeMode[r % 5];
    double qScaleA = kModeQ[typeA - 1];
    double qScaleB = kModeQ[typeB - 1];

    double sr = globalConfig.sampleRate;
    double nyq = sr * 0.49;
    double g = 2.0 * sin(M_PI / N_RATIO);

    // new slow drift targets once per block (heavily smoothed per-sample -> ~Hz wander)
    I.driftTgtA = DRIFT * lcgBipolar(I.rng);
    I.driftTgtB = DRIFT * lcgBipolar(I.rng);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      double kA = cutAb[i];
      double kB = cutBb[i];
      double res = CLAMP(0.0f, 1.0f, resb[i]);
      double drive = gainb[i];

      // clock Hz with measured inverse cross-coupling
      double fca = clockHz(kA) * (1.0 - XCOUPLE * CLAMP(0.0, 1.0, kB));
      double fcb = clockHz(kB) * (1.0 - XCOUPLE * CLAMP(0.0, 1.0, kA));

      I.driftA += DRIFT_SMOOTH * (I.driftTgtA - I.driftA);
      I.driftB += DRIFT_SMOOTH * (I.driftTgtB - I.driftB);

      double fclkA = fca * N_RATIO * (1.0 + I.driftA); if (fclkA > nyq) fclkA = nyq;
      double fclkB = fcb * N_RATIO * (1.0 + I.driftB); if (fclkB > nyq) fclkB = nyq;

      // Clock Src tunes each filter (both filters always run): 0=A both use clk A,
      // 1=B both use clk B, 2=Both -> A on clk A / B on clk B (divergent -> comb,
      // self-osc, breathing). Only Both diverges, so only Both self-oscs (faithful).
      double incA = (clk == 1 ? fclkB : fclkA) / sr;
      double incB = (clk == 0 ? fclkA : fclkB) / sr;

      double qBase = fmax(0.004, 1.0 / (1.0 + res * 40.0));
      double x = softclip(drive * (double)in[i]);

      // shared resonance loop (output -> filter inputs): self-osc only in Both mode
      // (divergent clocks) past res~0.7. Single-clock (converged) never self-oscs.
      double kfb = (clk == 2 && res > 0.7) ? (res - 0.7) / 0.3 * 0.95 : 0.0;
      double fb = I.fbk;

      double a = coreStep(I.phA, incA, I.lpA, I.bpA, I.hpA, I.heldA, I.envA,
                          x + kfb * fb, qBase * qScaleA, g, FEEDTHRU, typeA);
      double y;
      if (parallel)
      {
        // A and B both filter the input; sum. (Clock Src still sets each clock.)
        double b = coreStep(I.phB, incB, I.lpB, I.bpB, I.hpB, I.heldB, I.envB,
                            x + kfb * fb, qBase * qScaleB, g, FEEDTHRU, typeB);
        y = 0.5 * (a + b);
      }
      else
      {
        // Series: A's tap feeds B.
        double b = coreStep(I.phB, incB, I.lpB, I.bpB, I.hpB, I.heldB, I.envB,
                            a, qBase * qScaleB, g, FEEDTHRU, typeB);
        y = b;
      }
      I.fbk = y;

      if (alias == 2) // HI = mild anti-alias HF smoothing
      {
        double s = 0.6 * y + 0.4 * I.aliasPrev;
        I.aliasPrev = y;
        y = s;
      }
      else
      {
        I.aliasPrev = y;
      }

      out[i] = (float)y;
    }
  }

} // namespace stolmine
