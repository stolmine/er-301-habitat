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

// NOTE (am335x hardening, milestone 2): this is the FULL-FIDELITY first pass per
// planning/vitrail-unit.md (build it right, then harden). Before hardware:
//   - replace per-sample powf (cutoffHz) with a fast exp2 approx / block-rate update
//   - replace tanh with a rational fast-saturate
//   - hoist the block-constant `mode` switch out of the per-sample loop (branchless
//     tap select) per feedback_runtime_branched_dsp_dispatch
//   - evaluate float vs double for the SVF state (feedback_cortex_a8_no_double)
//   - confirm file-level -fno-tree-vectorize + objdump has no NEON :64/:128 hints

namespace stolmine
{

  static const double N_RATIO = 25.0;   // switched-cap clock = cutoff * N (measured)
  static const double XCOUPLE = 0.06;   // each cutoff pulls the OTHER clock down ~6%
  static const double FEEDTHRU = 0.03;  // clock feedthrough into each core
  static const double DRIFT = 0.05;     // analog clock drift depth (+/-5%)
  static const double DRIFT_SMOOTH = 0.00005; // per-sample smoothing -> ~Hz-scale wander

  // mode -> shared SVF-feedback scale (MODE_Q); index = mode-1.
  // 1=LP 2=BP 3=HP 4=Notch 5=AP 6=Hidden
  static const double kModeQ[6] = {1.0, 1.0, 0.7, 1.3, 1.1, 0.9};

  static inline uint32_t lcgNext(uint32_t &s) { s = s * 1103515245u + 12345u; return s; }
  static inline double lcgBipolar(uint32_t &s)
  {
    return (double)((lcgNext(s) >> 8) & 0xFFFF) / 32767.5 - 1.0;
  }

  static inline double cutoffHz(double k)
  {
    if (k < 0.0) k = 0.0; else if (k > 1.0) k = 1.0;
    double e = k / 0.8; if (e > 1.0) e = 1.0;
    return 30.0 * pow(166.6667, e);       // 30 Hz .. 5 kHz, saturating past 0.8
  }

  static inline double softclip(double x)   // odd-dominant symmetric clip
  {
    if (x > 1.0) return 0.66667;
    if (x < -1.0) return -0.66667;
    return x - x * x * x * (1.0 / 3.0);
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
                                double &hp, double &held, double xin, double q,
                                double g, double ft, int mode)
  {
    ph += inc;
    if (ph >= 1.0)
    {
      ph -= 1.0;
      double sq = (ph < 0.5) ? 1.0 : -1.0;
      double in2 = xin + ft * sq;
      hp = in2 - lp - q * bp;
      bp += g * hp; bp = 1.5 * tanh(bp * 0.66667);
      lp += g * bp; lp = 1.5 * tanh(lp * 0.66667);
      held = tapSel(mode, lp, bp, hp, q);
    }
    return held;
  }

  struct Vitrail::Internal
  {
    // core A
    double phA = 0.0, lpA = 0.0, bpA = 0.0, hpA = 0.0, heldA = 0.0;
    // core B (phase offset keeps the two clocks non-degenerate)
    double phB = 0.31, lpB = 0.0, bpB = 0.0, hpB = 0.0, heldB = 0.0;
    double fbk = 0.0;        // shared resonance loop (B out -> A in)
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
    addOption(mMode);
    addOption(mClkSrc);
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

    int mode = CLAMP(1, 6, (int)(mMode.value() + 0.5f));
    int clk = CLAMP(1, 3, (int)(mClkSrc.value() + 0.5f));
    int alias = CLAMP(1, 2, (int)(mAlias.value() + 0.5f));

    double sr = globalConfig.sampleRate;
    double nyq = sr * 0.49;
    double g = 2.0 * sin(M_PI / N_RATIO);
    double modeQ = kModeQ[mode - 1];

    // new slow drift targets once per block (heavily smoothed per-sample -> ~Hz wander)
    I.driftTgtA = DRIFT * lcgBipolar(I.rng);
    I.driftTgtB = DRIFT * lcgBipolar(I.rng);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      double kA = cutAb[i];
      double kB = cutBb[i];
      double res = CLAMP(0.0f, 1.0f, resb[i]);
      double drive = gainb[i];

      // measured inverse cross-coupling (each cutoff pulls the other clock down)
      double fca = cutoffHz(kA) * (1.0 - XCOUPLE * CLAMP(0.0, 1.0, kB));
      double fcb = cutoffHz(kB) * (1.0 - XCOUPLE * CLAMP(0.0, 1.0, kA));

      I.driftA += DRIFT_SMOOTH * (I.driftTgtA - I.driftA);
      I.driftB += DRIFT_SMOOTH * (I.driftTgtB - I.driftB);

      double fclkA = fca * N_RATIO * (1.0 + I.driftA); if (fclkA > nyq) fclkA = nyq;
      double fclkB = fcb * N_RATIO * (1.0 + I.driftB); if (fclkB > nyq) fclkB = nyq;
      double incA = fclkA / sr;
      double incB = fclkB / sr;

      double q = fmax(0.004, 1.0 / (1.0 + res * 40.0)) * modeQ;
      double x = softclip(drive * (double)in[i]);

      double y;
      if (clk == 1)
      {
        y = coreStep(I.phA, incA, I.lpA, I.bpA, I.hpA, I.heldA, x, q, g, FEEDTHRU, mode);
      }
      else if (clk == 2)
      {
        y = coreStep(I.phB, incB, I.lpB, I.bpB, I.hpB, I.heldB, x, q, g, FEEDTHRU, mode);
      }
      else
      {
        // Both: two cores cascaded (each own clock) + a shared resonance loop.
        // Comb tracks the low clock; convergence stacks to the 2x peak; the loop
        // self-oscillates past res~0.7 (only exists with both cores -> single-clock
        // never self-oscs); divergent clocks -> two pitches beat -> breathing.
        double kfb = res > 0.7 ? (res - 0.7) / 0.3 * 0.95 : 0.0;
        double a = coreStep(I.phA, incA, I.lpA, I.bpA, I.hpA, I.heldA,
                            x + kfb * I.fbk, q, g, FEEDTHRU, mode);
        double b = coreStep(I.phB, incB, I.lpB, I.bpB, I.hpB, I.heldB,
                            a, q, g, FEEDTHRU, mode);
        I.fbk = b;
        y = b;
      }

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
