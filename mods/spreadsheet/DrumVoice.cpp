#include "DrumVoice.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

// no-tree-vectorize is load-bearing on am335x (feedback_disable_tree_vectorize_am335x);
// applied globally for this package via mod.mk CFLAGS.

namespace stolmine
{
  // ---------------------------------------------------------------------------
  // Engine transplant (2.8.3.72): the oscillator/FM/membrane core that used to
  // live in this file is gone, replaced verbatim by the modal lattice engine
  // from Tessera.cpp - itself fitted to the Trinity BLOCK mode-structure
  // campaign (1143 hardware captures: fine 1-D throws for all 8 controls + all
  // 28 2-D pair matrices; analysis: ~/repos/trinity-midi-harness/analysis-
  // modemap.md). Ngoma's 14-parameter surface, trigger/hold/attack shell,
  // punch stage, and output chain (variable clipper, EQ, compressor, level)
  // survive; see planning/ngoma-tessera-integration.md section 4 for the
  // param-mapping decisions. Tessera.cpp/.h are the frozen reference model -
  // every table below is copied byte-for-byte from there; do not edit one
  // without the other unless intentionally diverging.
  //
  // KEY STRUCTURAL FINDING (Tessera): the hardware spectrum is NOT stretched
  // odd harmonics. Every capture factorizes as f(h,k) = fc*(h + k*r): a core
  // oscillator (odd harmonics h) cross-modulated by a 2nd oscillator at
  // fc*(1+r), giving uniformly-spaced intermod sidebands. ~30% of sideband
  // amplitude sits BELOW fc (the k=-1,-2 "sub" modes) - that is the
  // hardware's body/punch, which a stretched-harmonic model cannot produce at
  // all.
  //
  // Two decay classes (measured): "tone" modes (carrier / osc2 / folded core
  // harmonics) ring at tauRatio ~0.93-1.0; ALL sidebands are short (~0.35,
  // weak ones 0.15-0.2). That class split IS the measured brightness-decay
  // mechanism. All control interdependencies reduce to analytic forms (no 2-D
  // tables needed).
  // ---------------------------------------------------------------------------
  static const int NM = 16;

  // mode (h,k): f = fc*(h + k*r)
  // 12 fitted modes + 4 GENERATIVE fold lanes (m8/9/10/14 = h3/h5/h7/h9 of the
  // carrier, m12/m15 = 3fB/5fB of oscB). Character campaign: the hardware voice
  // is two oscillators morphed sine->triangle and WAVEFOLDED; the fold is
  // odd-symmetric so it makes only odd harmonics of each parent, and those lanes
  // are now painted from the exact Fourier series of the read fold law (see the
  // Character block below), not from fitted per-mode regressions.
  static const float kH[NM] = {1, 1, 1, 3, 1, 3, 1, 5, 3, 5, 7, 3, 3, 5, 9, 5};
  static const float kK[NM] = {0, 1, -1, 1, 2, 2, -2, 2, 0, 0, 0, -1, 3, 1, 0, 5};
  // Per-mode amplitude, least-squares fitted against all 1143 hardware captures
  // (fit_amps.py). amp = c0 + c1*dL + c2*foldN + c3*r + c4*lf + c5*g.
  // The r term is included ONLY for the modes where the stored data shows a real
  // r-dependence (3,4,5,7,8,10,11). For the carrier, osc2, both sub modes and (5,0)
  // the measured amplitude is FLAT in r (e.g. sub = 0.226/0.243/0.236 at r=0.20/0.39/
  // 0.58) - earlier fits produced a large negative r slope there purely because 370 of
  // 423 samples sit at r=0.39, and that phantom slope was collapsing the sub (the
  // body/punch) by up to 5x at mid Shape.
  // Column c2 (formerly foldN) is retired: Character's effect on every lane is
  // now generative (see the Character block below), so the fitted foldN slopes
  // are zeroed and the feature is gone. Rows m8/9/10/12/14/15 are fully
  // generative lanes; their regressions are unused (zeroed).
  static const float kAmpFit[NM][6] = {
    { 0.9935f,  0.0070f,  0.0000f,  0.0000f, -0.0122f, -0.0801f},  // h=1 k=+0 carrier
    { 0.6483f,  0.0306f,  0.0000f,  0.0000f, -0.0482f, -0.0203f},  // h=1 k=+1 osc2
    { 0.3075f,  0.0240f,  0.0000f,  0.0000f, -0.0278f, -0.0301f},  // h=1 k=-1 sub
    { 0.2088f,  0.0251f,  0.0000f,  0.0141f, -0.0347f,  0.0450f},  // h=3 k=+1
    { 0.3876f, -0.0014f,  0.0000f, -0.1182f, -0.0597f,  0.0165f},  // h=1 k=+2
    { 0.1923f,  0.0130f,  0.0000f, -0.0632f, -0.0428f,  0.1504f},  // h=3 k=+2
    { 0.2590f, -0.0566f,  0.0000f,  0.0000f, -0.1133f,  0.2749f},  // h=1 k=-2 sub
    { 0.1107f, -0.0036f,  0.0000f, -0.0136f,  0.0175f,  0.3865f},  // h=5 k=+2
    { 0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f},  // h=3 k=+0 generative
    { 0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f},  // h=5 k=+0 generative
    { 0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f},  // h=7 k=+0 generative
    { 0.1542f, -0.0047f,  0.0000f,  0.3891f, -0.0221f,  0.0759f},  // h=3 k=-1 (kCrossPaint-dead)
    { 0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f},  // h=3 k=+3 generative (3fB)
    { 0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f},  // h=5 k=+1 (kCrossPaint-dead)
    { 0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f},  // h=9 k=+0 generative
    { 0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f},  // h=5 k=+5 generative (5fB)
  };

  // ---- Character: GENERATIVE morph + wavefold (firmware read, instruction level;
  // Character campaign 2026-07-23, planning/ngoma-character-campaign.md).
  //
  // The hardware's Character knob does two things to BOTH oscillators, in sequence:
  //   blend = clamp(2*char, 0, 1)  sine -> triangle waveform morph (bottom half)
  //   w     = clamp(2*char, 1, 2)  wavefold drive (top half)
  //   fold(mix) = (|u - round(u)| - 0.25)*4,  u = w*mix*0.25 + 0.25
  // The fold is odd-symmetric: it generates ONLY odd harmonics of each parent,
  // riding the parent's envelope (the measured 0.89-1.0 tau ratios of the "tone"
  // modes). Confirmed on a fresh 162-capture transparent-output sweep: h2 = 0
  // everywhere, h3 NULL at CC80, h7 null at CC112-120, h9 null at CC96, carrier
  // a1(char) non-monotonic within 1-2% of theory on 3 notes.
  //
  // blend and w are functions of ONE knob, so the harmonic curves are 1-D. The
  // five tables below are the exact Fourier series of the folded waveform,
  // evaluated offline over the knob throw with the fitted calibration
  // (char_eff = min(cc/124, 0.970), w gain 0.95, triangle harmonic softness
  // 0.85; typical residual x1.42 vs hardware across 4 harmonics x 12 chars x
  // 3 notes, including the nulls - the painted table had +13 dB class errors).
  // kCharA1 is normalized at the corpus fit point (char CC48) so every fitted
  // kAmpFit intercept keeps its operating point; R tables are SIGNED (a
  // negative ratio becomes a half-turn phase offset - the products are
  // coherent, not arbitrary). Fold ratios additionally scale with parent pitch
  // as (f_parent/253.6)^-0.128 (measured; one exponent explains both the
  // per-note trend and the carrier-vs-oscB family offset).
  // The voice carries TRANSPARENT-output harmonic levels; at high Clipper the
  // drive+limiter stage regenerates the extra brightness exactly as the
  // hardware's does (same generate-dont-paint architecture as kCrossPaint).
  static const float kCharA1[33] = {
      +1.1424f, +1.1306f, +1.1189f, +1.1071f, +1.0953f, +1.0835f, +1.0718f,
      +1.0600f, +1.0482f, +1.0364f, +1.0247f, +1.0129f, +1.0011f, +0.9893f,
      +0.9776f, +0.9658f, +0.9791f, +1.0216f, +1.0492f, +1.0645f, +1.0698f,
      +1.0668f, +1.0568f, +1.0410f, +1.0202f, +0.9952f, +0.9666f, +0.9349f,
      +0.9005f, +0.8638f, +0.8250f, +0.8127f, +0.8127f
  };
  static const float kCharR3[33] = {
      +0.0000f, -0.0050f, -0.0100f, -0.0152f, -0.0204f, -0.0258f, -0.0313f,
      -0.0370f, -0.0427f, -0.0486f, -0.0546f, -0.0608f, -0.0671f, -0.0736f,
      -0.0802f, -0.0869f, -0.0900f, -0.0762f, -0.0513f, -0.0188f, +0.0189f,
      +0.0605f, +0.1051f, +0.1521f, +0.2014f, +0.2530f, +0.3071f, +0.3641f,
      +0.4245f, +0.4890f, +0.5585f, +0.5809f, +0.5809f
  };
  static const float kCharR5[33] = {
      +0.0000f, +0.0018f, +0.0036f, +0.0055f, +0.0074f, +0.0093f, +0.0113f,
      +0.0133f, +0.0154f, +0.0175f, +0.0197f, +0.0219f, +0.0242f, +0.0265f,
      +0.0289f, +0.0313f, +0.0315f, +0.0173f, -0.0068f, -0.0350f, -0.0637f,
      -0.0907f, -0.1147f, -0.1353f, -0.1523f, -0.1658f, -0.1761f, -0.1836f,
      -0.1884f, -0.1910f, -0.1915f, -0.1913f, -0.1913f
  };
  static const float kCharR7[33] = {
      -0.0000f, -0.0009f, -0.0018f, -0.0028f, -0.0038f, -0.0047f, -0.0058f,
      -0.0068f, -0.0078f, -0.0089f, -0.0100f, -0.0112f, -0.0123f, -0.0135f,
      -0.0147f, -0.0160f, -0.0154f, -0.0015f, +0.0194f, +0.0399f, +0.0554f,
      +0.0645f, +0.0670f, +0.0638f, +0.0562f, +0.0454f, +0.0325f, +0.0183f,
      +0.0036f, -0.0110f, -0.0253f, -0.0295f, -0.0295f
  };
  static const float kCharR9[33] = {
      -0.0000f, +0.0006f, +0.0011f, +0.0017f, +0.0023f, +0.0029f, +0.0035f,
      +0.0041f, +0.0047f, +0.0054f, +0.0061f, +0.0068f, +0.0075f, +0.0082f,
      +0.0089f, +0.0097f, +0.0088f, -0.0044f, -0.0213f, -0.0330f, -0.0363f,
      -0.0318f, -0.0220f, -0.0100f, +0.0020f, +0.0123f, +0.0197f, +0.0239f,
      +0.0245f, +0.0218f, +0.0159f, +0.0135f, +0.0135f
  };
  // Fold-ratio pitch droop (measured -0.128, clamped for out-of-map pitches).
  static const float kCharDroopExp = -0.128f;
  static const float kCharDroopRef = 1.0f / 253.6f;

  // tone modes ring long, sidebands short; the generative fold lanes are tone
  // class (they ride the parent envelope; measured 0.89-0.95).
  static const float kTauR[NM] = {1.00f, 0.94f, 0.35f, 0.35f, 0.35f, 0.34f,
                                  0.20f, 0.17f, 0.95f, 0.95f, 0.93f, 0.35f,
                                  0.89f, 0.60f, 0.93f, 0.89f};

  // ---- Trinity output stage (measured: findings-clipper.md, 152 hardware captures) ----
  // A soft clip on the SUMMED output, proven acting on the sum rather than per-partial
  // (hardware output peak stays pinned at 0.203-0.206 across every timbre while crest
  // collapses 11.1 -> 3.0). Level-invariant to ~4% across a 5-point velocity sweep, so a
  // memoryless curve is the right model:  y = g * x / sqrt(1 + (x/th)^2).
  //
  // Transparent below CC ~12; threshold falls 0.221 -> 0.070 across CC 16-40; above CC 40
  // the threshold is fixed and the knob is pure drive, with makeup gain asymptoting ~4.03.
  //
  // th is tabulated in HARDWARE capture units; kSMH converts to this model's units
  // (model base peak 2.6143 <-> hardware unclipped base peak 0.2030). That conversion
  // matters because a saturator is nonlinear: unlike every earlier fit, absolute scale
  // is not free here.
  static const float kSMH = 0.2030f / 2.6143f;
  // Pre-clipper drive. The hardware's output peak is PINNED at 0.262 with CV 6.8% across
  // all 1120 cells of the timbre map, because its voice always drives the limiter into
  // limiting. Our voice did not: at Grit 108 the model peaked at 0.04, far below threshold,
  // so no limiting happened and peak swung 15.8x across the Grit throw (hardware: 1.10x).
  // Driving harder pins the peak the same way the hardware does. Measured on the map:
  // spread 15.8x -> 2.2x, and crest error +75% -> +7% (crest was one of the three largest
  // gaps). Higher drive keeps flattening peak but overshoots crest (-11% at 30, -18% at 60)
  // and stretches apparent decay, so 12 is the calibrated point.
  // P4 (plan 4.3): the 12x became the TOP of the Clipper throw rather than a fixed
  // constant. driveLinear = 1 + clipper*(kDriveMax-1); at clipper=1.0 the stage is
  // bit-identical (pre-makeup) to the fixed-12x parity-proven state (the corpus
  // point / frozen-Tessera heft); backing the knob off progressively releases the
  // modal bank's real pre-drive dynamics (the Shape/Grit crest variation the fixed
  // drive was flattening). Shape campaign P0: the SHIPPED DEFAULT moved to
  // clipper=0.0 - the unit ships clean and the knob adds heft. Threshold stays
  // pinned at the CC48 corpus tables - the state every amp coefficient was fitted
  // against; the equal-loudness makeup is re-anchored at the clean home (below).
  static const float kDriveMax = 12.0f;
  // P2 (shape campaign): the cross-lattice modes are now GENERATIVE, not painted.
  // Firmware (FUN_2400b1d8): the voice is two folded oscillators (fc, fc*(1+r))
  // mixed linearly; every corpus mode with k != 0 and h-k != 0 reindexes exactly
  // as (h-k)*fc + k*fB - intermod products of the OUTPUT LIMITER, not voice
  // content. Measured on the fresh grit=0 Shape grid (180 captures, per-capture
  // empirical fc/r): cross modes collapse -46.7 dB (n=200) when the hardware
  // output stage goes transparent (CC8) - they are absent from the voice. And
  // the painted kAmpFit cross amplitudes measured +13 dB hot vs hardware at the
  // corpus point itself (123 pairs, iqr +8.6..+19.7) - a per-mode error the
  // band-energy grid was blind to, and the measured root of "Shape reads as
  // muddy waveshaping": a static, too-loud, phase-arbitrary painted lattice on
  // top of the real one. With the painted cross lanes silenced (kCrossPaint=0),
  // this model's own drive+sqrt-limiter stage generates the product lattice from
  // the two real partial families: measured +7.6 dB vs hardware at the corpus
  // point (generated-only probe, 123 pairs, iqr +0.7..+15.6) - closer than any
  // painted variant tried (+13.0 painted, +8.4 trimmed-painted), with real
  // product phases, real within-hit evolution, and threshold-tracking across
  // the whole Clipper throw for free. Next lever for the +7.6 dB residual:
  // pre-clip crest calibration against transparent-clipper hardware captures.
  static const float kCrossPaint = 0.0f;
  static const float kClipTh[16] = {9e9f, 9e9f, 0.221f, 0.145f, 0.095f, 0.070f, 0.070f,
                                    0.070f, 0.070f, 0.070f, 0.070f, 0.070f, 0.070f,
                                    0.070f, 0.070f, 0.070f};
  static const float kClipG[16] = {1.000f, 1.000f, 1.538f, 2.150f, 2.648f, 3.037f, 3.329f,
                                   3.546f, 3.701f, 3.810f, 3.888f, 3.940f, 3.976f, 4.001f,
                                   4.021f, 4.034f};

  // PRESENCE GATE: RETIRED (Character campaign). The gated lanes were the h=3/5/7
  // core harmonics + (3,-1); the harmonics are now generative fold lanes whose
  // curves are genuinely ~0 with the fold shut (no gate needed), and (3,-1) is a
  // kCrossPaint-silenced cross lane. The fitted logistic coefficients live in git
  // history (kGateFit, 2.8.3.52-2.8.3.77) if the cross lanes are ever revisited.

  // ---- Grit = common-mode noise FM (measured: findings-grit.md, 155 captures) ----
  // Grit does NOT primarily add a noise bed. It frequency-modulates the oscillators with
  // noise, which is why hardware repeats decorrelate (5 identical hits correlate 0.999 at
  // Grit 0 and 0.13 at Grit 64) while their spectra stay stable to 0.02%: same spectrum,
  // new waveform every hit. A magnitude-STFT corpus is blind to this, which is how 1143
  // captures missed it.
  //
  // The deviation is COMMON-MODE: measured absolute jitter is constant (1.4x spread)
  // across partials spanning 33x in frequency, so it is ONE shared phase modulation
  // applied to every mode, not per-mode noise and not proportional to frequency. Depth
  // scales with the fundamental: depth = kappa(grit) * fc.
  //
  // kappa at CC 0,8,...,120 from the note-60 sweep. The first three entries are the
  // 0.10% analysis floor and are therefore zero, matching the measured dead zone.
  // Note the shape: it PEAKS at CC 56-64 and falls back by CC 88, then the separate
  // additive-noise regime takes over above CC 115. Four regimes, not one ramp.
  static const float kGritKappa[16] = {
    0.0000f, 0.0000f, 0.0000f, 0.0072f, 0.0207f, 0.0352f, 0.0478f, 0.0550f,
    0.0541f, 0.0372f, 0.0306f, 0.0096f, 0.0100f, 0.0091f, 0.0095f, 0.0196f};
  // modulator bandwidth: one-pole at 40 Hz reproduces the measured hit-to-hit
  // decorrelation across the whole throw (model 0.78/0.34/0.16 vs hardware
  // 0.75/0.37/0.13 at Grit 24/32/64). 1.85 centres the residual depth bias.
  static const float kGritLpHz = 40.0f;
  static const float kGritDepthTrim = 1.85f;

  static inline uint32_t lcg(uint32_t &s) { s = s * 1103515245u + 12345u; return s; }
  static inline float noise(uint32_t &s) { return (float)((lcg(s) >> 9) & 0xFFFF) / 32768.0f - 1.0f; }

  // The measured amp table holds EXTRACTOR-measured amplitudes, which for short-tau
  // modes are already partly decayed over the ~110 ms analysis window. Convert a
  // measured amp back to the true initial amp: W(tau) = (tau/T)(1-e^-T/tau) is the
  // window-average of the decay; initial = measured * W(tau_carrier)/W(tau_mode).
  // Without this the sub/sideband modes render ~2x too quiet (measured across the grid).
  static inline float winAvg(float tau_s)
  {
    const float Tw = 0.11f;
    if (tau_s < 1e-4f) return 1e-4f;
    return (tau_s / Tw) * (1.0f - expf(-Tw / tau_s));
  }

  // The hardware amplitude envelope is a CUBED LINEAR RAMP, not an exponential (firmware:
  // the level jumps to 1.0, then decrements linearly, and the audio path uses level^3).
  // We render each mode's decay as (ramp)^3 with ramp falling 1 -> 0 over a duration T.
  // rampDurMs(tau) maps a mode's exponential time-constant (the quantity the mode extractor
  // and all our fitted decay laws report) to the ramp duration T whose log-linear-fit tau
  // equals it - so the swap is tau-preserving and the fitted tables stay valid. The fit was
  // measured against the extractor: T = 9.952*tau^0.8445 (reproduces 100/200/400/800/1600 ms
  // ramps read back as 15/36/79/167/430 ms tau). The amp correction stays the exponential
  // winAvg above: the amp table was regression-fit against exp-winAvg-corrected initial amps,
  // and re-deriving it for the cubed shape (tried) regressed sub_amp without helping decay.
  static inline float rampDurMs(float tau_ms)
  {
    if (tau_ms < 0.1f) tau_ms = 0.1f;
    return 9.952f * powf(tau_ms, 0.8445f);
  }

  // measured HF tau ceiling: no cap below ~600 Hz, ~500 ms @950 Hz, ~60 ms @1.4 kHz+
  static inline float hfCeilMs(float f)
  {
    if (f <= 600.0f) return 1.0e9f;
    if (f >= 1400.0f) return 60.0f;
    if (f <= 950.0f)
    {
      float t = (logf(f) - logf(600.0f)) / (logf(950.0f) - logf(600.0f));
      return 3000.0f * powf(500.0f / 3000.0f, t);
    }
    float t = (logf(f) - logf(950.0f)) / (logf(1400.0f) - logf(950.0f));
    return 500.0f * powf(60.0f / 500.0f, t);
  }

  static inline float fast_tanh(float x)
  {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  // IEEE 754 fast log2 / exp2 / dB helpers (from Larets/Parfait).
  static inline float fast_log2(float x)
  {
    union { float f; int32_t i; } v;
    v.f = x;
    float y = (float)(v.i);
    y *= 1.0f / (1 << 23);
    y -= 127.0f;
    return y;
  }
  static inline float fast_exp2(float x)
  {
    union { float f; int32_t i; } v;
    v.i = (int32_t)((x + 127.0f) * (1 << 23));
    return v.f;
  }
  static inline float fast_log10(float x) { return fast_log2(x) * 0.30103f; }
  static inline float fast_fromDb(float db) { return fast_exp2(db * 0.16609640474f); }

  // Polynomial sine for triangle-input ([-1,1]) -> sine([-pi/2, pi/2]).
  // 7th-order odd-power approx, max error ~7e-6 over the input domain --
  // below the retired sineLUT's lerp error. This is the substitution for
  // the modal bank's per-mode sineLUT() gather: Cortex-A8 NEON has no
  // gather load (feedback_neon_no_gather_lut_dsp), so a per-lane indexed
  // LUT read cannot cross-mode vectorize; polynomial substitution is the
  // proven escape (JF, Visadhara precedent). Used by the scalar (non-ARM)
  // fallback of the modal kernel in process() -- the NEON path inlines the
  // identical polynomial on quads (same coefficients, same wrap semantics)
  // so linux emu and am335x hardware run the same algorithm.
  static inline float polySine(float tri)
  {
    float t2 = tri * tri;
    float t4 = t2 * t2;
    float t6 = t4 * t2;
    return tri * (1.5707963f - 0.6459640f * t2 + 0.0796921f * t4 - 0.0046816f * t6);
  }

  struct DrumVoice::Internal
  {
    // ---- Tessera modal engine state (verbatim engine; copied field-for-field
    // from Tessera::Internal). Sized [16]: only NM=14 modes are ever written
    // at trigger time; pad slots [14]/[15] are explicitly zeroed on every
    // trigger (phase/env/mfreq/mfreqSr/mdecay/ramp all 0 -- algebraically
    // silent: ramp=0 keeps r3=0 regardless of phase/mfreq drift) so the P5
    // NEON kernel can process all 4 quads unconditionally, no tail loop.
    float phase[16], env[16], mfreq[16], mdecay[16];
    float ramp[16];              // cubed-ramp decay state (1 -> 0), cubed at output
    // Block-baked per-mode phase increment (mfreq/sr), set at trigger time.
    // Removes a per-mode-per-sample division from the modal kernel (P5).
    float mfreqSr[16];
    // P2 (shape campaign): per-lane onset-bloom mask, baked at trigger.
    // Measured (fit_bloom.py, 72 grit=0 cells): the osc-B family (h-k=0 lanes,
    // (1,1)/(3,3)) starts ~37% LOW and fades in as 1 - 0.61*exp(-t/(2*sweepTime))
    // -- the firmware's pitch-envelope-swept fold_B center (p4*0.5+0.5). Other
    // lanes measured flat (harmC +-6%) or covered by their fast decay classes
    // (cross +15% early, inside iqr noise); their mask is 0.
    float bloomAmt[16];
    float bloomEnv = 0.0f, bloomCoeff = 0.0f;
    float pitchEnv = 0, pitchCoeff = 0.999f, startMult = 1;
    float noiseEnv = 0, noiseCoeff = 0, noiseLp = 0, burst = 0, burstCoeff = 0;
    float jitLp = 0, jitHz = 0;          // common-mode FM state + per-hit depth
    uint32_t jitRng = 0x9e3779b9u;       // own stream: must not perturb the noise bed
    int holdLeft = 0;
    float prevTrig = 0;
    uint32_t rng = 0x51ee7u;

    // ---- Ngoma-specific additions on top of the modal engine ----
    // Attack (4.4): linear ramp 0->1 multiplying the modal bank sum only.
    // Firmware has no attack (hard jump); default attack=0 reproduces that.
    float atkEnv = 1.0f, atkIncr = 0.0f;
    // Punch: Ngoma flavor Tessera has no equivalent of. Post-noise-mix,
    // pre-drive multiplicative transient boost with its own short decay.
    float punchEnv = 0.0f;

    // Output-chain state (EQ + compressor), kept verbatim from pre-transplant
    // DrumVoice -- engine-agnostic, unaffected by the transplant.
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
    float compDetector = 0.0f;

    float vizEnvLevel = 0.0f;
    bool vizGateState = false;

    float cachedCharacter = 0.5f;
    float cachedShape = 0.0f;
    float cachedGrit = 0.0f;

    Internal() { for (int m = 0; m < 16; m++) { phase[m] = 0; env[m] = 0; mfreq[m] = 0; mfreqSr[m] = 0; mdecay[m] = 0; ramp[m] = 0; bloomAmt[m] = 0; } }
  };

  DrumVoice::DrumVoice()
  {
    addInput(mTrigger);
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mCharacter);
    addParameter(mShape);
    addParameter(mGrit);
    addParameter(mPunch);
    addParameter(mSweep);
    addParameter(mSweepTime);
    addParameter(mAttack);
    addParameter(mHold);
    addParameter(mDecay);
    addParameter(mClipper);
    addParameter(mEQ);
    addParameter(mLevel);
    addParameter(mCompAmt);
    addParameter(mOctave);
    mpInternal = new Internal();
  }

  DrumVoice::~DrumVoice()
  {
    delete mpInternal;
  }

  float DrumVoice::getCharacter() { return mpInternal->cachedCharacter; }
  float DrumVoice::getShape()     { return mpInternal->cachedShape; }
  float DrumVoice::getGrit()      { return mpInternal->cachedGrit; }
  float DrumVoice::getEnvLevel()  { return mpInternal->vizEnvLevel; }
  bool  DrumVoice::getGateState() { return mpInternal->vizGateState; }

  void DrumVoice::process()
  {
    Internal &s = *mpInternal;
    float *trig = mTrigger.buffer();
    float *voct = mVOct.buffer();
    float *out  = mOut.buffer();

    float sr = globalConfig.sampleRate;
    float nyq = sr * 0.45f;
    float invSr = 1.0f / sr;   // block-rate; the modal kernel uses this instead of a per-sample /sr

    float character = CLAMP(0.0f, 1.0f, mCharacter.value());
    float shape     = CLAMP(0.0f, 1.0f, mShape.value());
    float grit      = CLAMP(0.0f, 1.0f, mGrit.value());
    float punch     = CLAMP(0.0f, 1.0f, mPunch.value());
    float sweep     = CLAMP(0.0f, 1.0f, mSweep.value());
    float sweepTime = CLAMP(0.001f, 0.5f, mSweepTime.value());
    float attack    = CLAMP(0.0f, 0.05f, mAttack.value());
    float hold      = CLAMP(0.0f, 0.5f, mHold.value());
    float decay     = CLAMP(0.01f, 2.0f, mDecay.value());
    float clipperParam = CLAMP(0.0f, 1.0f, mClipper.value());
    float eq        = CLAMP(-1.0f, 1.0f, mEQ.value());
    float level     = CLAMP(0.0f, 1.0f, mLevel.value());
    float compAmt   = CLAMP(0.0f, 1.0f, mCompAmt.value());
    bool compActive = compAmt > 0.001f;

    // CPR single-band one-knob comp (matches Larets pattern). Auto makeup
    // is always on so the user gets compensated loudness as they push.
    float compThresholdDb = -compAmt * 40.0f;            // 0 dB -> -40 dB
    float compRatioI      = 1.0f / (1.0f + compAmt * 19.0f); // 1:1 -> 20:1
    float compAttackSec   = 0.010f - compAmt * 0.009f;   // 10 ms -> 1 ms
    float compReleaseSec  = 0.200f;
    float compRiseCoeff   = expf(-1.0f / (compAttackSec * sr));
    float compFallCoeff   = expf(-1.0f / (compReleaseSec * sr));
    float compMakeupGain  = compActive
        ? fast_fromDb(-compThresholdDb * (1.0f - compRatioI))
        : 1.0f;

    s.cachedCharacter = character;
    s.cachedShape = shape;
    s.cachedGrit = grit;

    // V/Oct + octave offset: block-rate (unchanged from pre-transplant
    // DrumVoice). fc itself is computed per-trigger below, at the trigger
    // sample's V/Oct value.
    float octave = floorf(CLAMP(-4.0f, 4.0f, mOctave.value()) + 0.5f);

    // ---- Tessera block-rate laws (verbatim unless marked ADAPTED) ----
    // knobs 0..1 -> the hardware's CC 0..127 throw (all laws fitted in CC domain)
    // (ccChar retired: the Character LUTs are indexed by the knob directly,
    // with the CC mapping baked into the tables at generation time.)
    float ccShape = shape * 127.0f;
    float ccGrit = grit * 127.0f;
    // Ngoma's Sweep dial is normalized 0..1; map onto Tessera's measured
    // CC 0..127 law. (Old 0..72 default 18 = 0.25 here, ccSweep 31.75 - same.)
    float ccSweep = sweep * 127.0f;

    float jitCoeff = 1.0f - expf(-6.2832f * kGritLpHz / globalConfig.sampleRate);
    // noise() is uniform (std 1/sqrt(3)); the one-pole scales variance by a/(2-a).
    // Normalise both so kappa*fc IS the resulting deviation in Hz.
    float jitNorm = 1.7320508f * kGritDepthTrim / sqrtf(jitCoeff / (2.0f - jitCoeff));

    // Clipper stage (P4 -> P2 shape campaign): the knob drives BOTH the pre-clip
    // drive (below) and the stage's effective hardware CC. P4 pinned the tables
    // at the corpus CC48 point; measurement showed that leaves the stage
    // NON-transparent at clipper=0 (drive 1 still clips peaks at th~0.9 and
    // sprays intermod products +42 dB above the hardware's transparent output,
    // which is genuinely product-free at CC8, th=9e9 below CC12). The hardware
    // knob's own semantics raise the threshold to transparency at the bottom,
    // so the model's throw now morphs ccEff = 8 + 40*clipper: bit-identical
    // CC48 tables at clipper=1 (every fitted coefficient's operating point),
    // truly transparent at clipper=0.
    float clipTh, clipG;
    {
      float ccClip = 8.0f + 40.0f * clipperParam;
      float u = CLAMP(0.0f, 15.0f, ccClip / 8.0f);
      int ci = (int)u;
      if (ci > 14) ci = 14;
      float cf = u - (float)ci;
      clipTh = (kClipTh[ci] + (kClipTh[ci + 1] - kClipTh[ci]) * cf) / kSMH;
      clipG = (kClipG[ci] + (kClipG[ci + 1] - kClipG[ci]) * cf) / 3.329f;
    }
    // Variable drive (P4): linear throw 1..kDriveMax; clipper=1.0 is the parity
    // anchor (bit-identical pre-makeup to the fixed 12x stage). Keep the measured
    // sqrt-law curve: at top-of-throw it IS the frozen reference by identity, and
    // stacking a second, unmeasured curve (tanh) under the same knob was rejected
    // in the P4 A/B (crest collapses harder at equal ct; see the integration log).
    float driveLinear = 1.0f + clipperParam * (kDriveMax - 1.0f);
    // Equal-loudness makeup, RE-ANCHORED at clipper = 0 (shape campaign P0,
    // REFIT after the P2 threshold morph): the unit ships CLEAN, and the whole
    // stage (clipG(0)*makeup(0)) is NET UNITY at the home position - the clean
    // voice passes at its natural level; the rest of the throw is attenuated to
    // equal loudness, so pushing Clipper changes crest/density, not loudness.
    // The leading 3.329 is exactly 1/clipG(0) (the CC8 point of the measured
    // hardware gain table under the /3.329 corpus normalisation). Cubic
    // numerator over driveLinear, least-squares fitted to ngoma-mirror RMS of
    // the shipped default hit at 11 throw points with the ccEff-morphing stage
    // (max ripple 0.5 dB; the mid-throw knee where the threshold lands is the
    // ripple source). Net stage gain at clipper = 1 is 0.3583 (-8.9 dB): the
    // corpus-heft top plays at the clean default's loudness. Absolute-scale
    // parity vs hardware captures at clipper = 1 must divide this factor out
    // (the pre-makeup signal there is bit-identical to the P4 corpus stage).
    float makeup = 3.329f *
                   (1.0f + clipperParam * (-2.632211f +
                    clipperParam * (6.107731f - clipperParam * 3.183871f)))
                   / driveLinear;
    float blockClipGain = clipG * makeup;

    // Shape -> osc2 detune ratio (measured linear, pitch-tracked)
    float r = CLAMP(0.0f, 2.0f, 0.0189f * (ccShape - 9.2f));
    // Shape campaign P2: painted cross-lattice lanes are silenced (see the
    // kCrossPaint comment block up top for the full evidence chain); the
    // product lattice is generated by the clipper stage from the two real
    // partial families instead.
    // Character: generative morph+fold curves, 33-point LUT over the knob throw
    // (see the kCharA1/kCharR* block). Baked per trigger like every other amp.
    float chA1, chR3, chR5, chR7, chR9;
    {
      float cu = character * 32.0f;
      int cli = (int)cu;
      if (cli > 31) cli = 31;
      float clf = cu - (float)cli;
      chA1 = kCharA1[cli] + (kCharA1[cli + 1] - kCharA1[cli]) * clf;
      chR3 = kCharR3[cli] + (kCharR3[cli + 1] - kCharR3[cli]) * clf;
      chR5 = kCharR5[cli] + (kCharR5[cli + 1] - kCharR5[cli]) * clf;
      chR7 = kCharR7[cli] + (kCharR7[cli + 1] - kCharR7[cli]) * clf;
      chR9 = kCharR9[cli] + (kCharR9[cli + 1] - kCharR9[cli]) * clf;
    }
    // ADAPTED: Ngoma's Decay dial is honest seconds (0.01..2.0 s), so tauC is the
    // dial value directly -- this REPLACES Tessera's CC decay law (which derived
    // tauC from a fitted quadratic-in-log CC curve). Everything downstream of tauC
    // (grit collapse, L/dL, kTauR classes, rampDurMs) is untouched.
    float tauC = decay * 1000.0f;   // ms

    // Grit -> decay-time scaling above the 0.75 breakpoint.
    //
    // The hardware does NOT impose a tau ceiling on mode decay (that model was refuted
    // empirically) and does NOT add a damping rate. Above grit 0.75 it scales the
    // decay-time PARAMETER itself, linearly, reaching exactly zero at grit 1.0.
    // Breakpoint and slope are exact; the zone is entered only above 0.75.
    //
    // The tone collapsing toward silence at max grit is faithful, not a defect: the
    // noise path carries its own envelope and survives, which is the "just noise"
    // behaviour at the top of the throw. Independently, this is the same mechanism we
    // heard and logged as "amp-env shortening past ~0.75 (808 snare)".
    //
    // kGritDecFloor keeps tau off zero (expf(-1/0) is a division by zero) and is the
    // only free parameter here.
    static const float kGritDecBreak = 0.75f;
    static const float kGritDecSlope = 4.0f;
    static const float kGritDecFloor = 1.0f / 3000.0f;   // tauC * this ~ 1 ms at the top
    float gN = ccGrit / 127.0f;
    float tauEff = tauC;
    if (gN > kGritDecBreak)
    {
      float k = 1.0f - (gN - kGritDecBreak) * kGritDecSlope;
      tauEff = tauC * (k > kGritDecFloor ? k : kGritDecFloor);
    }
    float L = logf(tauC);            // spectral-reshaping driver (pre-ceiling)
    float dL = L - 5.489f;           // L0 = ln(242 ms)

    // Grit additive-noise path. The firmware confirms grit is noise-FM (the jitter path
    // below), and that the genuine mix-out to "just noise" happens ONLY above the 0.75
    // breakpoint (CC 95.25). The old additive noise bed from CC 25 was compensating for FM
    // depth the magnitude-fit under-rendered - a fudge, not a mechanism. Removing it below
    // 95.25 is neutral on the grid and slightly improves e_hi/e_up (the exact broadband
    // bands the bed was polluting); the measured kGritKappa jitter now carries all grit
    // character below 0.75, as the mechanism says it should. The top regime is unchanged.
    float noiseMix = 0.0f;
    if (ccGrit >= 115.0f) noiseMix = 0.75f;
    else if (ccGrit > 110.0f) noiseMix = 0.35f + (ccGrit - 110.0f) / 5.0f * 0.40f;
    else if (ccGrit > 95.25f) noiseMix = CLAMP(0.0f, 0.35f, 0.0074f * (ccGrit - 18.0f));
    float noiseTau = tauEff < 150.0f ? tauEff : 150.0f;
    if (ccGrit > 110.0f) noiseTau = 60.0f;

    // Sweep -> start pitch multiplier (linear in OCTAVES); verbatim.
    float startMult = 1.12f * powf(2.0f, ccSweep / 22.5f);
    // ADAPTED: Ngoma's SweepTime dial is honest seconds (0.001..0.5 s) and IS tauP
    // directly -- this REPLACES Tessera's CC time law (0.002*exp(0.042*ccTime)).
    float tauP = sweepTime;
    float pitchCoeff = expf(-1.0f / (tauP * sr));
    // ADAPTED: Ngoma's Hold dial is honest seconds (0..0.5 s) and gives holdSamples
    // directly -- this REPLACES Tessera's CC hold law (0.001*2^(ccHold/8.6), 4s clamp);
    // the 0.5s param range makes that clamp moot here.
    int holdSamples = (int)(hold * sr);
    float noiseLpG = 1.0f - expf(-6.2832f * 4000.0f / sr);

    // Ngoma addition: Punch decay coefficient (Tessera has no punch stage).
    // Computed at block rate per the plan's Punch mapping (4.2).
    float punchDecayCoeff = expf(-1.0f / (0.003f * sr));

    // DJ filter coefficients (block-rate, unchanged from pre-transplant
    // DrumVoice). EQ is bipolar: -1..0 = LP, 0 = bypass, 0..+1 = HP.
    // Magnitude drives cutoff sweep.
    float eqCut = eq;
    float absEqCut = fabsf(eqCut);
    bool filterActive = absEqCut >= 0.01f;
    bool isLP = (eqCut < 0.0f);
    float fA1 = 0.0f, fA2 = 0.0f, fA3 = 0.0f, fK = 1.05f;
    if (filterActive)
    {
      float filterFreq = isLP
        ? 20.0f * powf(1000.0f, 1.0f - absEqCut)
        : 20.0f * powf(1000.0f, absEqCut);
      filterFreq = CLAMP(20.0f, sr * 0.49f, filterFreq);
      float g = tanf(3.14159f * filterFreq / sr);
      fA1 = 1.0f / (1.0f + g * (g + fK));
      fA2 = g * fA1;
      fA3 = g * fA2;
    }

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float tv = trig[i];
      if (tv > 0.5f && s.prevTrig <= 0.5f)   // rising edge -> new hit
      {
        // ADAPTED: fc comes from Ngoma's V/Oct + Octave law (no separate F0
        // knob, matching the pre-transplant DrumVoice control surface), not
        // from Tessera's F0 parameter. Same CLAMP(8,8000) range as Tessera.
        float fc = 110.0f * powf(2.0f, voct[i] + octave);
        fc = CLAMP(8.0f, 8000.0f, fc);
        float lf = logf(fc / 246.5f);                      // pitch feature
        // gN (grit feature) is hoisted to the block scope above
        // Fold-ratio pitch droop, per parent oscillator (measured -0.128; one
        // exponent explains the per-note trend AND the carrier-vs-oscB offset).
        float droopC = powf(fc * kCharDroopRef, kCharDroopExp);
        droopC = CLAMP(0.5f, 1.5f, droopC);
        float droopB = powf(fc * (1.0f + r) * kCharDroopRef, kCharDroopExp);
        droopB = CLAMP(0.5f, 1.5f, droopB);
        float aCar = 0.0f, aOscB = 0.0f;   // parent amps, stashed for the fold lanes

        for (int m = 0; m < NM; m++)
        {
          float f = fc * (kH[m] + kK[m] * r);
          s.mfreq[m] = f;
          s.mfreqSr[m] = f * invSr;   // baked phase-increment-per-sample (P5)
          const float *c = kAmpFit[m];
          float a = c[0] + c[1] * dL + c[3] * r + c[4] * lf + c[5] * gN;
          // Parent lanes carry the fold's own fundamental trajectory a1(char):
          // non-monotonic (dip at the morph top, recover, fall to 0.81 at full
          // fold), measured within 1-2% on three notes.
          if (m == 0 || m == 1) a *= chA1;
          if (a < 0.0f) a = 0.0f;
          if (a > 1.5f) a = 1.5f;
          if (m == 0) aCar = a;
          if (m == 1) aOscB = a;
          // GENERATIVE fold lanes: exact harmonic series of the folded parent.
          // A negative signed ratio is a half-turn phase offset (set below).
          float chSign = 0.0f;
          switch (m)
          {
            case 8:  a = aCar  * fabsf(chR3) * droopC; chSign = chR3; break;
            case 9:  a = aCar  * fabsf(chR5) * droopC; chSign = chR5; break;
            case 10: a = aCar  * fabsf(chR7) * droopC; chSign = chR7; break;
            case 14: a = aCar  * fabsf(chR9) * droopC; chSign = chR9; break;
            case 12: a = aOscB * fabsf(chR3) * droopB; chSign = chR3; break;
            case 15: a = aOscB * fabsf(chR5) * droopB; chSign = chR5; break;
            default: break;
          }
          // P2: cross modes are limiter products, not voice content - the
          // painted lanes are silenced and the clipper stage generates the
          // real product lattice from the surviving families. Voice-generated
          // modes (k=0 carrier harmonics, h-k=0 osc-B family) are untouched.
          if (kK[m] != 0 && (kH[m] - kK[m]) != 0) a *= kCrossPaint;
          if (f <= 15.0f || f >= nyq) a = 0.0f;           // drop out-of-range modes
          // measured-amp -> initial-amp correction (see winAvg)
          float tmS = tauEff * kTauR[m] * 0.001f;
          if (kTauR[m] < 0.5f)   // ceiling applies to sidebands only, not tone modes
          {
            float ceilS = hfCeilMs(f) * 0.001f;
            if (tmS > ceilS) tmS = ceilS;
          }
          if (tmS < 0.002f) tmS = 0.002f;
          a *= winAvg(tauEff * 0.001f) / winAvg(tmS);
          s.env[m] = a;               // static initial amplitude; the ramp carries the decay
          s.ramp[m] = 1.0f;
          // P2 onset bloom: osc-B family lanes (h-k = 0) fade in over the
          // pitch-envelope window (measured 1 - 0.61*exp(-t/20ms) at the
          // hardware's time-CC40 point; tied to sweepTime, see bloomCoeff).
          s.bloomAmt[m] = (kK[m] != 0 && (kH[m] - kK[m]) == 0) ? 0.61f : 0.0f;
          // FIRMWARE start-phase scheme (shape0-level campaign): on each trigger
          // the hardware hard-resets BOTH parent phase accumulators to exactly
          // 0.0 (r8-gated skip of the accumulator reload, verified at
          // instruction level: 0x2400b508/0x2400b50e osc B, same pattern osc C;
          // reset constant DAT_2400b740 = 0x00000000). The +0.25 quarter-turn
          // in the firmware is applied only to the TABLE INDEX, and the table
          // is cos-form: the effective oscillator is -sin(2*pi*phase), so both
          // parents start ALIGNED AT A ZERO CROSSING. That is simultaneously
          //  (a) the coherent sum: at shape ~ 0 the parents collapse onto fc
          //      and add in phase - measured shape-0 fundamental 1.65-1.69x
          //      the separated carrier, uniform across Character; and
          //  (b) the click-tamer: a sinusoid starting at its zero crossing has
          //      no value discontinuity, so the instant envelope (no attack
          //      ramp in the firmware) produces no broadband splash. Measured:
          //      hardware onset HF(>4k) fraction 0.0000 in the first 4 ms.
          // The old varied phases (Tessera f774e02) existed to kill the click
          // caused by all-lanes-at-+0.25 (cosine PEAK) alignment; zero-crossing
          // alignment kills it the way the hardware does. The global -sin
          // polarity is an unobservable overall sign (every fold harmonic is
          // odd), so lanes use logical phase 0 = sin rising.
          // Fold lanes: harmonic n of a parent starting at sin-zero is
          // b_n*sin(n*theta) - it starts at n*0 = 0 plus a half turn when the
          // signed series coefficient is negative. Cross lanes are silenced
          // (kCrossPaint); the drive+limiter stage sees the hardware-true
          // alignment when it generates intermods.
          // P5 CONVENTION OFFSET (unchanged): the triangle+polySine path
          // renders -cos(2*pi*p) = sin(2*pi*(p - 0.25)); baking +0.25 into the
          // stored phase makes the kernel render sin(2*pi*(logical phase)).
          {
            float logical = (chSign < 0.0f) ? 0.5f : 0.0f;
            s.phase[m] = logical + 0.25f;
            s.phase[m] -= floorf(s.phase[m]);
          }
          float tm = tauEff * kTauR[m];
          float ceil = (kTauR[m] < 0.5f) ? hfCeilMs(f) : 1e9f;
          if (tm > ceil) tm = ceil;
          if (tm < 2.0f) tm = 2.0f;
          float Tramp = rampDurMs(tm) * 0.001f;      // ramp duration (s), tau-preserving
          s.mdecay[m] = 1.0f / (Tramp * sr);         // per-sample linear ramp decrement
        }
        // (Character campaign: NM is now 16 - the two former padding lanes are
        // the generative (9,0) and (5,5) fold lanes, so the 4-quad NEON kernel's
        // 16 unconditional lanes are all live and the P5 pad-zero loop is gone.)
        // P2 onset bloom carrier: one shared scalar exp decay (1 -> 0), lane
        // factor = 1 - bloomAmt[m]*bloomEnv. Time constant tied to the pitch
        // envelope (firmware: the fold_B center IS p4). 2*sweepTime reproduces
        // the measured ~20 ms fade at the capture point (time CC40 ~ 10.7 ms);
        // the x2 scaling is calibrated at that single point (see campaign doc).
        s.bloomEnv = 1.0f;
        s.bloomCoeff = expf(-1.0f / (2.0f * sweepTime * sr));
        s.startMult = startMult;
        s.pitchCoeff = pitchCoeff;
        s.pitchEnv = 1.0f;
        s.holdLeft = holdSamples;
        s.noiseEnv = 1.0f;
        s.noiseCoeff = expf(-1.0f / (noiseTau * 0.001f * sr));
        // regime-4 attack burst (measured punch 9.8 / atk_frac 0.41 at grit >= ~115)
        s.burst = (ccGrit >= 115.0f) ? 8.0f : 0.0f;
        s.burstCoeff = expf(-1.0f / (0.005f * sr));
        {
          float u = CLAMP(0.0f, 15.0f, ccGrit / 8.0f);
          int gi = (int)u; if (gi > 14) gi = 14;
          float gf = u - (float)gi;
          s.jitHz = (kGritKappa[gi] + (kGritKappa[gi + 1] - kGritKappa[gi]) * gf) * fc;
        }

        // ---- Ngoma additions: attack ramp + punch (Tessera has neither) ----
        // Attack (4.4): default 0 = authentic instant-on (firmware hard jump).
        if (attack > 0.0001f)
        {
          s.atkEnv = 0.0f;
          s.atkIncr = 1.0f / (attack * sr);
        }
        else
        {
          s.atkEnv = 1.0f;
          s.atkIncr = 0.0f;
        }
        // Punch (KEEP, Ngoma flavor): drops the old aboveKnee grit coupling --
        // the Ngoma grit-knee system dies with the rest of the old engine.
        s.punchEnv = punch;
        s.vizGateState = true;
      }
      s.prevTrig = tv;

      s.pitchEnv *= s.pitchCoeff;
      float pmul = 1.0f + (s.startMult - 1.0f) * s.pitchEnv;
      bool held = s.holdLeft > 0;

      // one shared noise-FM deviation in Hz, added identically to every mode
      s.jitLp += jitCoeff * (noise(s.jitRng) - s.jitLp);
      float jitDev = s.jitLp * jitNorm * s.jitHz;

      // ---- Modal bank kernel (P5 NEON pass) ----
      // 14 modes padded to 16 lanes = 4 NEON quads on am335x; identical
      // scalar fallback on linux/x86 so emu exercises the same algorithm
      // the hardware runs (same poly-sine coefficients, same negative-safe
      // floor wrap -- jitDev can transiently drive the phase increment
      // negative at low fc + high grit, so a forward-only wrap is wrong
      // here). pmul, jitDevSr, heldMul are per-sample scalars, common-mode
      // across every lane (the measured grit mechanism is one shared
      // noise-FM deviation applied identically to every mode -- see the
      // Grit comment block above -- which is exactly the NEON-friendly
      // shape: one broadcast per sample, not per-lane state).
      float jitDevSr = jitDev * invSr;
      float heldMul = held ? 0.0f : 1.0f;
      // P2 onset-bloom carrier: one scalar exp decay per sample, broadcast to
      // the lanes below (lane factor 1 - bloomAmt*bloomEnv; bloomAmt is 0 on
      // all but the osc-B family lanes, so most lanes multiply by exactly 1).
      s.bloomEnv *= s.bloomCoeff;
      float y;
#ifdef __ARM_NEON
      {
        float32x4_t pmulV     = vdupq_n_f32(pmul);
        float32x4_t jitDevSrV = vdupq_n_f32(jitDevSr);
        float32x4_t heldMulV  = vdupq_n_f32(heldMul);
        float32x4_t bloomEnvV = vdupq_n_f32(s.bloomEnv);
        float32x4_t zeroV     = vdupq_n_f32(0.0f);
        float32x4_t oneV      = vdupq_n_f32(1.0f);
        float32x4_t acc       = vdupq_n_f32(0.0f);

        for (int q = 0; q < 4; q++)
        {
          float *ph = &s.phase[4 * q];
          float *fr = &s.mfreqSr[4 * q];
          float *md = &s.mdecay[4 * q];
          float *rm = &s.ramp[4 * q];
          float *ev = &s.env[4 * q];
          float *bm = &s.bloomAmt[4 * q];

          float32x4_t phase = vld1q_f32(ph);
          float32x4_t freq  = vld1q_f32(fr);
          float32x4_t mdec  = vld1q_f32(md);
          float32x4_t ramp  = vld1q_f32(rm);
          float32x4_t envv  = vld1q_f32(ev);

          // phase advance: inc = mfreqSr*pmul + jitDevSr (common-mode)
          float32x4_t inc = vmlaq_f32(jitDevSrV, freq, pmulV);
          phase = vaddq_f32(phase, inc);

          // negative-safe wrap to [0,1): floor via truncate-toward-zero,
          // corrected down by 1 where the truncation rounded up (fp > ph
          // only happens for negative non-integer phase).
          int32x4_t   ipart = vcvtq_s32_f32(phase);
          float32x4_t fpart = vcvtq_f32_s32(ipart);
          uint32x4_t  negMask = vcgtq_f32(fpart, phase);
          float32x4_t adj = vbslq_f32(negMask, oneV, zeroV);
          float32x4_t floorP = vsubq_f32(fpart, adj);
          phase = vsubq_f32(phase, floorP);
          vst1q_f32(ph, phase);

          // ramp: decrement masked by heldMul (0 while held), clamp >= 0
          ramp = vsubq_f32(ramp, vmulq_f32(mdec, heldMulV));
          ramp = vmaxq_f32(ramp, zeroV);
          vst1q_f32(rm, ramp);
          float32x4_t r3 = vmulq_f32(vmulq_f32(ramp, ramp), ramp);

          // triangle: tri = 4*min(phase, 1-phase) - 1
          float32x4_t inv = vsubq_f32(oneV, phase);
          float32x4_t mn  = vminq_f32(phase, inv);
          float32x4_t tri = vsubq_f32(vmulq_n_f32(mn, 4.0f), oneV);

          // 7th-order polynomial sine (matches scalar polySine exactly)
          float32x4_t t2 = vmulq_f32(tri, tri);
          float32x4_t t4 = vmulq_f32(t2, t2);
          float32x4_t t6 = vmulq_f32(t4, t2);
          float32x4_t poly = vmlsq_f32(vdupq_n_f32(1.5707963f), t2, vdupq_n_f32(0.6459640f));
          poly = vmlaq_f32(poly, t4, vdupq_n_f32(0.0796921f));
          poly = vmlsq_f32(poly, t6, vdupq_n_f32(0.0046816f));
          float32x4_t sine = vmulq_f32(tri, poly);

          // P2 onset bloom: factor = 1 - bloomAmt*bloomEnv (vmls), 1.0 on
          // non-B lanes by construction (bloomAmt 0).
          float32x4_t bloomA = vld1q_f32(bm);
          float32x4_t bloomF = vmlsq_f32(oneV, bloomA, bloomEnvV);
          acc = vmlaq_f32(acc, sine, vmulq_f32(vmulq_f32(envv, r3), bloomF));
        }

        float32x2_t sumPair = vadd_f32(vget_high_f32(acc), vget_low_f32(acc));
        sumPair = vpadd_f32(sumPair, sumPair);
        y = vget_lane_f32(sumPair, 0);
      }
#else
      {
        y = 0.0f;
        for (int m = 0; m < 16; m++)
        {
          float inc = s.mfreqSr[m] * pmul + jitDevSr;
          s.phase[m] += inc;
          s.phase[m] -= floorf(s.phase[m]);      // negative-safe floor == NEON wrap by construction
          s.ramp[m] -= s.mdecay[m] * heldMul;
          if (s.ramp[m] < 0.0f) s.ramp[m] = 0.0f;
          float r3 = s.ramp[m] * s.ramp[m] * s.ramp[m];   // cubed linear ramp
          float tri = 4.0f * (s.phase[m] < 0.5f ? s.phase[m] : 1.0f - s.phase[m]) - 1.0f;
          float bloomF = 1.0f - s.bloomAmt[m] * s.bloomEnv;   // P2 onset bloom
          y += s.env[m] * r3 * bloomF * polySine(tri);
        }
      }
#endif
      if (held) s.holdLeft--;

      // Ngoma addition: linear attack ramp on the modal bank sum ONLY (4.4).
      // Applied here -- after the modal sum, before the noise-body mix below --
      // so the measured regime-4 noise burst is never softened by the attack.
      if (s.atkEnv < 1.0f)
      {
        s.atkEnv += s.atkIncr;
        if (s.atkEnv > 1.0f) s.atkEnv = 1.0f;
      }
      y *= s.atkEnv;

      // noise body (own env) + grit attack burst
      s.noiseEnv *= s.noiseCoeff;
      s.burst *= s.burstCoeff;
      s.noiseLp += noiseLpG * (noise(s.rng) - s.noiseLp);
      y = y * (1.0f - noiseMix * 0.5f) + s.noiseLp * (noiseMix * s.noiseEnv + s.burst * 0.12f);

      // Ngoma addition: Punch, post-noise-mix, pre-drive/clip (drops the old
      // droopFreq pitch coupling along with the rest of the old engine).
      y *= (1.0f + s.punchEnv);
      s.punchEnv *= punchDecayCoeff;
      if (s.punchEnv < 1e-5f) s.punchEnv = 0.0f;

      float yd = y * driveLinear;
      float ct = yd / clipTh;
      // __builtin_sqrtf maps straight to VFP vsqrt.f32. Plain sqrtf() left GCC emitting
      // an out-of-line `bl sqrtf` fallback in the per-sample loop, which is both slow on
      // Cortex-A8 and an AAPCS call barrier inside the audio path. 1+ct*ct is provably
      // >= 1, so the fallback is dead weight.
      // ADAPTED: Tessera fuses `* level` into this line; Ngoma keeps its existing output
      // chain order (clip -> EQ -> comp -> level), so `level` is deferred to the final
      // `out[i] = sample * level` below instead. blockClipGain = clipG * makeup (P4).
      float sample = blockClipGain * yd / __builtin_sqrtf(1.0f + ct * ct);

      // DJ filter (TPT SVF, Cytomic formulation) -- unchanged from pre-transplant DrumVoice.
      if (filterActive)
      {
        float v0 = sample;
        float v3 = v0 - s.ic2eq;
        float v1 = fA1 * s.ic1eq + fA2 * v3;
        float v2 = s.ic2eq + fA2 * s.ic1eq + fA3 * v3;
        s.ic1eq = 2.0f * v1 - s.ic1eq;
        s.ic2eq = 2.0f * v2 - s.ic2eq;
        float wet = isLP ? v2 : (v0 - fK * v1 - v2);
        sample = sample * (1.0f - absEqCut) + wet * absEqCut;
      }

      // CPR single-band one-knob comp (replaces Makeup). Auto makeup
      // built in. Bypassed when compAmt < 0.001. Unchanged from
      // pre-transplant DrumVoice.
      if (compActive)
      {
        float absLevel = sample < 0.0f ? -sample : sample;
        float coeff = absLevel > s.compDetector ? compRiseCoeff : compFallCoeff;
        s.compDetector = coeff * s.compDetector + (1.0f - coeff) * absLevel;
        float levelDb = 20.0f * fast_log10(s.compDetector + 1e-10f);
        float overDb = levelDb - compThresholdDb;
        if (overDb < 0.0f) overDb = 0.0f;
        float reductionDb = overDb * (1.0f - compRatioI);
        sample *= fast_fromDb(-reductionDb) * compMakeupGain;
      }

      out[i] = sample * level;
    }

    // Viz pollers (cube graphic): env[0]*ramp[0]^3 is the carrier's LIVE
    // value (deliberate fix vs Tessera's getEnvLevel(), which returns the
    // static initial amplitude -- see plan section 4.5).
    s.vizEnvLevel = s.env[0] * s.ramp[0] * s.ramp[0] * s.ramp[0];
    s.vizGateState = (s.ramp[0] > 0.0f);
  }

} // namespace stolmine
