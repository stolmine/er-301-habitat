#include "DrumVoice.h"
#include "DrumVoiceSineLUT.h"   // static half-sine LUT (no runtime trig on am335x)
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
  static const int NM = 14;

  // mode (h,k): f = fc*(h + k*r)
  // 12 core modes plus two that exist only once Character opens the fold: measured on the
  // 4-D map, (3,3) rises 0.000 -> 0.229 and (5,1) 0.000 -> 0.122 from ch0 to ch127.
  static const float kH[NM] = {1, 1, 1, 3, 1, 3, 1, 5, 3, 5, 7, 3, 3, 5};
  static const float kK[NM] = {0, 1, -1, 1, 2, 2, -2, 2, 0, 0, 0, -1, 3, 1};
  // Per-mode amplitude, least-squares fitted against all 1143 hardware captures
  // (fit_amps.py). amp = c0 + c1*dL + c2*foldN + c3*r + c4*lf + c5*g.
  // The r term is included ONLY for the modes where the stored data shows a real
  // r-dependence (3,4,5,7,8,10,11). For the carrier, osc2, both sub modes and (5,0)
  // the measured amplitude is FLAT in r (e.g. sub = 0.226/0.243/0.236 at r=0.20/0.39/
  // 0.58) - earlier fits produced a large negative r slope there purely because 370 of
  // 423 samples sit at r=0.39, and that phantom slope was collapsing the sub (the
  // body/punch) by up to 5x at mid Shape.
  static const float kAmpFit[NM][6] = {
    { 0.9935f,  0.0070f,  0.0049f,  0.0000f, -0.0122f, -0.0801f},  // h=1 k=+0 carrier
    { 0.6483f,  0.0306f, -0.0419f,  0.0000f, -0.0482f, -0.0203f},  // h=1 k=+1 osc2
    { 0.3075f,  0.0240f, 0.0000f,  0.0000f, -0.0278f, -0.0301f},  // h=1 k=-1 sub
    { 0.2088f,  0.0251f,  0.0000f,  0.0141f, -0.0347f,  0.0450f},  // h=3 k=+1
    { 0.3876f, -0.0014f,  0.0000f, -0.1182f, -0.0597f,  0.0165f},  // h=1 k=+2
    { 0.1923f,  0.0130f,  0.0000f, -0.0632f, -0.0428f,  0.1504f},  // h=3 k=+2
    { 0.2590f, -0.0566f,  0.0000f,  0.0000f, -0.1133f,  0.2749f},  // h=1 k=-2 sub
    { 0.1107f, -0.0036f,  0.0488f, -0.0136f,  0.0175f,  0.3865f},  // h=5 k=+2
    { 0.1667f,  0.0504f,  0.2011f,  0.1799f, -0.0446f,  0.0166f},  // h=3 k=+0
    { 0.0500f,  0.0267f, -0.0601f,  0.0000f,  0.0003f,  0.2468f},  // h=5 k=+0 (base 0.19->0.05: HW h5~0 at low Character)
    { 0.0000f,  0.0023f, -0.0553f, -0.0391f,  0.0055f,  0.4403f},  // h=7 k=+0 (base 0.16->0: HW has no h7 until the fold opens)
    { 0.1542f, -0.0047f, -0.1143f,  0.3891f, -0.0221f,  0.0759f},  // h=3 k=-1
    { 0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f},  // h=3 k=+3 fold-only
    { 0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f,  0.0000f},  // h=5 k=+1 fold-only
  };

  // ---- Character is a spectrum SWAP, not an additive fold (measured, 4-D map, grit 0/40,
  // all shapes and notes, median per-mode amplitude relative to the carrier, ch0 -> ch127):
  //   KILLED: (1,-1) 0.233->0  (3,1) 0.177->0  (1,2) 0.094->0  (3,2) 0.070->0
  //           (1,-2) 0.538->0.154
  //   RAISED: (3,0) 0.050->0.472   (3,-1) 0.020->0.192   (5,0) 0->0.099
  //   NEW:    (3,3) 0->0.229       (5,1) 0->0.122
  // Five fitted foldN coefficients had the WRONG SIGN - they boosted what the hardware
  // kills - which is why Character "did much less" on the model than on the hardware.
  //
  // The raise is ADDITIVE, not a crossfade override. An override (a = a*(1-fold) + target
  // *fold) makes the amplitude a fixed constant at full fold, discarding the fitted c1..c5
  // dependence on decay, shape, pitch and GRIT - which blew up e_hi/e_sub/crest at high
  // grit when tried. Adding a fold-scaled delta keeps those measured slopes alive.
  // Applied in the RAW fold, not pitch-trimmed foldN: the hardware's Character effect is
  // pitch-flat across four octaves, while the -0.163 trim collapsed the model's response
  // ~2.8x from n36 to n84 - worst exactly where woodblocks and snares live.
  static const float kFoldKill[NM] = {1.00f, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.29f,
                                      1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f};
  // m8 (3,0) and m11 (3,-1) carry the measured h3 boost that IS Character's signature effect
  // (HW h3 0.25->0.47 as the fold opens). Refit stronger (0.28->0.45, 0.25->0.40) so the boost
  // actually lands; the aggregate-error minimum wanted them near zero, which neutered Character.
  static const float kFoldRaise[NM] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                       0.4500f, 0.0053f, 0.0f, 0.4000f, 0.5725f, 0.3050f};
  // tone modes ring long, sidebands short
  static const float kTauR[NM] = {1.00f, 0.94f, 0.35f, 0.35f, 0.35f, 0.34f,
                                  0.20f, 0.17f, 0.95f, 0.95f, 0.93f, 0.35f,
                                  0.89f, 0.60f};

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
  // constant. driveLinear = 1 + clipper*(kDriveMax-1); at the shipped default
  // clipper=1.0 the stage is bit-identical to the fixed-12x parity-proven state
  // (the corpus point / frozen-Tessera heft); backing the knob off progressively
  // releases the modal bank's real pre-drive dynamics (the Shape/Grit crest
  // variation the fixed drive was flattening). Threshold and makeup stay pinned
  // at the CC48 corpus tables - the state every amp coefficient was fitted against.
  static const float kDriveMax = 12.0f;
  static const float kClipTh[16] = {9e9f, 9e9f, 0.221f, 0.145f, 0.095f, 0.070f, 0.070f,
                                    0.070f, 0.070f, 0.070f, 0.070f, 0.070f, 0.070f,
                                    0.070f, 0.070f, 0.070f};
  static const float kClipG[16] = {1.000f, 1.000f, 1.538f, 2.150f, 2.648f, 3.037f, 3.329f,
                                   3.546f, 3.701f, 3.810f, 3.888f, 3.940f, 3.976f, 4.001f,
                                   4.021f, 4.034f};

  // PRESENCE GATE (fit_gating.py, logistic on "was this mode detected", trained on
  // unswept captures only - at high Sweep the pitch has moved off fc so non-detection
  // is an analysis artifact, not absence).
  //
  // Applied ONLY to the h=3/5/7 core harmonics (modes 8-11). Those are genuinely absent
  // until Character opens the fold - the measured dead-zone below CC 78 - and their gate
  // coefficients are dominated by foldN (+2.4..+3.2). The amplitude fit gave them
  // intercepts of 0.15-0.19 because it only ever saw them when present (selection bias),
  // so without the gate the model renders core harmonics with the fold shut: too much
  // energy between the peaks (flatness 0.228 vs the hardware's 0.130).
  // The core modes (carrier/osc2/sub/sidebands) are NOT gated: their non-detections are
  // masking, not absence, and gating them would wrongly attenuate the fundamental.
  static const bool kGated[NM] = {false, false, false, false, false, false,
                                  false, false, true, true, true, true, false, false};
  static const float kGateFit[NM][6] = {
    {0,0,0,0,0,0}, {0,0,0,0,0,0}, {0,0,0,0,0,0}, {0,0,0,0,0,0},
    {0,0,0,0,0,0}, {0,0,0,0,0,0}, {0,0,0,0,0,0}, {0,0,0,0,0,0},
    {-1.6036f, -0.0850f, 2.8771f, 1.1738f,  0.0406f, 0.6956f},  // h=3 k=+0
    {-2.2754f,  0.2021f, 3.2338f, 1.5403f,  0.0160f, 0.8017f},  // h=5 k=+0
    {-2.2610f,  0.3351f, 3.0902f, 1.3819f, -0.2955f, 0.0801f},  // h=7 k=+0
    {-1.5053f,  0.3840f, 2.4148f, 0.5393f,  0.0810f, 0.7474f},  // h=3 k=-1
    {0,0,0,0,0,0}, {0,0,0,0,0,0},
  };

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

  static inline float sineLUT(float phase)   // phase in [0,1) -> sin(2*pi*phase)
  {
    phase -= floorf(phase);
    bool neg = phase >= 0.5f;
    float ph = neg ? (phase - 0.5f) : phase;
    float idx = ph * 512.0f;
    int i = (int)idx;
    float fr = idx - (float)i;
    float s = kDrumVoiceSineLUT[i] + fr * (kDrumVoiceSineLUT[i + 1] - kDrumVoiceSineLUT[i]);
    return neg ? -s : s;
  }

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
  // 7th-order odd-power approx, max error ~7e-6 over the input domain.
  // Retained unused by the P1 scalar port: this is the substitution the P5
  // NEON pass will use for the modal bank's sineLUT() gather (Cortex-A8 NEON
  // has no gather load; feedback_neon_no_gather_lut_dsp), following the same
  // pattern already proven in JF/Visadhara.
  static inline float polySine(float tri)
  {
    float t2 = tri * tri;
    float t4 = t2 * t2;
    float t6 = t4 * t2;
    return tri * (1.5707963f - 0.6459640f * t2 + 0.0796921f * t4 - 0.0046816f * t6);
  }

  // 4-lane phasor advance + polynomial sine. NEON-vectorized on am335x;
  // scalar fallback on x86/emu. CRITICAL: phases/inc/outSines MUST be
  // class member arrays (or otherwise heap-allocated) -- stack-locals
  // trigger GCC `:64` alignment hint under -O3 -ffast-math which traps
  // on Cortex-A8 (see feedback_neon_intrinsics_drumvoice memory).
  // Retained unused by the P1 scalar port -- this is the exact kernel shape
  // (SoA quads, common-mode broadcast, poly sine) the P5 NEON pass applies
  // to the modal bank's phase[16]/mfreq[16] arrays once they move to Internal.
  static inline void neonAdvanceSines(float *phases, const float *inc, float *outSines)
  {
#ifdef __ARM_NEON
    float32x4_t ph = vld1q_f32(phases);
    float32x4_t in = vld1q_f32(inc);
    ph = vaddq_f32(ph, in);
    // Wrap to [0,1): subtract floor. inc is non-negative; ph stays in
    // [0,1); after add ph in [0,2). vcvtq_s32_f32 truncates toward zero
    // == floor for non-negative values.
    int32x4_t   ipart = vcvtq_s32_f32(ph);
    float32x4_t fpart = vcvtq_f32_s32(ipart);
    ph = vsubq_f32(ph, fpart);
    // Triangle: tri = 4 * min(p, 1-p) - 1
    float32x4_t one  = vdupq_n_f32(1.0f);
    float32x4_t inv  = vsubq_f32(one, ph);
    float32x4_t mn   = vminq_f32(ph, inv);
    float32x4_t tris = vsubq_f32(vmulq_n_f32(mn, 4.0f), one);
    // 7th-order polynomial sine (matches scalar polySine):
    //   tri * (1.5708 - 0.6460*t^2 + 0.0797*t^4 - 0.00468*t^6)
    float32x4_t t2 = vmulq_f32(tris, tris);
    float32x4_t t4 = vmulq_f32(t2, t2);
    float32x4_t t6 = vmulq_f32(t4, t2);
    float32x4_t poly = vmlsq_f32(vdupq_n_f32(1.5707963f), t2, vdupq_n_f32(0.6459640f));
    poly = vmlaq_f32(poly, t4, vdupq_n_f32(0.0796921f));
    poly = vmlsq_f32(poly, t6, vdupq_n_f32(0.0046816f));
    float32x4_t sines = vmulq_f32(tris, poly);
    vst1q_f32(phases, ph);
    vst1q_f32(outSines, sines);
#else
    for (int j = 0; j < 4; j++) {
      phases[j] += inc[j];
      phases[j] -= floorf(phases[j]);
      float p = phases[j];
      float tri = 4.0f * (p < 0.5f ? p : 1.0f - p) - 1.0f;
      float t2 = tri * tri;
      float t4 = t2 * t2;
      float t6 = t4 * t2;
      outSines[j] = tri * (1.5707963f - 0.6459640f*t2 + 0.0796921f*t4 - 0.0046816f*t6);
    }
#endif
  }

  struct DrumVoice::Internal
  {
    // ---- Tessera modal engine state (verbatim engine; copied field-for-field
    // from Tessera::Internal). Sized [16]: only NM=14 modes are ever written
    // or read (pad slots [14]/[15] stay zero forever) -- the P5 NEON pass
    // pads to 4 quads, so the storage shape is already right.
    float phase[16], env[16], mfreq[16], mdecay[16];
    float ramp[16];              // cubed-ramp decay state (1 -> 0), cubed at output
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

    Internal() { for (int m = 0; m < 16; m++) { phase[m] = 0; env[m] = 0; mfreq[m] = 0; mdecay[m] = 0; ramp[m] = 0; } }
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

    float character = CLAMP(0.0f, 1.0f, mCharacter.value());
    float shape     = CLAMP(0.0f, 1.0f, mShape.value());
    float grit      = CLAMP(0.0f, 1.0f, mGrit.value());
    float punch     = CLAMP(0.0f, 1.0f, mPunch.value());
    float sweep     = CLAMP(0.0f, 72.0f, mSweep.value());
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
    float ccChar = character * 127.0f;
    float ccShape = shape * 127.0f;
    float ccGrit = grit * 127.0f;
    // ADAPTED: Ngoma's Sweep dial stays a 0..72 "semitones" throw (existing
    // control range); normalize it onto Tessera's measured CC 0..127 law
    // instead of introducing a second, CC-native Sweep control.
    float ccSweep = sweep * (127.0f / 72.0f);

    float jitCoeff = 1.0f - expf(-6.2832f * kGritLpHz / globalConfig.sampleRate);
    // noise() is uniform (std 1/sqrt(3)); the one-pole scales variance by a/(2-a).
    // Normalise both so kappa*fc IS the resulting deviation in Hz.
    float jitNorm = 1.7320508f * kGritDepthTrim / sqrtf(jitCoeff / (2.0f - jitCoeff));

    // Clipper threshold/makeup pinned at the corpus point (CC 48): that is the
    // operating point the whole 1143-capture corpus was recorded at, so it is the
    // state every amplitude coefficient in this file was fitted against. The
    // Clipper PARAM now drives the pre-clip DRIVE instead (P4, plan 4.3).
    float clipTh, clipG;
    {
      float ccClip = 48.0f;
      float u = CLAMP(0.0f, 15.0f, ccClip / 8.0f);
      int ci = (int)u;
      if (ci > 14) ci = 14;
      float cf = u - (float)ci;
      clipTh = (kClipTh[ci] + (kClipTh[ci + 1] - kClipTh[ci]) * cf) / kSMH;
      clipG = (kClipG[ci] + (kClipG[ci + 1] - kClipG[ci]) * cf) / 3.329f;
    }
    // Variable drive (P4): linear throw 1..kDriveMax; clipper=1.0 (default) is the
    // parity anchor (bit-identical to the fixed 12x stage). Keep the measured
    // sqrt-law curve: at top-of-throw it IS the frozen reference by identity, and
    // stacking a second, unmeasured curve (tanh) under the same knob was rejected
    // in the P4 A/B (crest collapses harder at equal ct; see the integration log).
    float driveLinear = 1.0f + clipperParam * (kDriveMax - 1.0f);
    // Equal-loudness makeup: with the pinned-threshold limiter, RMS drops as drive
    // backs off (crest rises 2.5 -> 7.7 across the throw - that is the point) but
    // loudness should not. Cubic in (1 - clipper), least-squares fitted against the
    // corpus reference hit's measured RMS ratio RMS(1)/RMS(c) at 8 points across
    // the throw (fit error <= 2.6%, i.e. <= 0.22 dB ripple); exactly 1.0 at the
    // default (clipper = 1) so the parity anchor stays bit-identical. Extrapolates
    // to 2.48 at clipper = 0 (measured 3.58; the +8 dB cap is deliberate - full
    // compensation would pump the low-drive noise floor). Consequence: at low
    // clipper the peak rises toward ~1.9x pre-level while loudness holds; that is
    // the released crest, trim Level if routing straight to a DAC.
    float mk = 1.0f - clipperParam;
    float makeup = 1.0f + mk * (0.646242f + mk * (-2.083327f + mk * 2.917451f));
    if (makeup > 2.512f) makeup = 2.512f;   // +8 dB cap
    float blockClipGain = clipG * makeup;

    // Shape -> osc2 detune ratio (measured linear, pitch-tracked)
    float r = CLAMP(0.0f, 2.0f, 0.0189f * (ccShape - 9.2f));
    // Character -> fold amount: dead zone to CC 78, then linear (when osc2 active)
    float fold = CLAMP(0.0f, 1.0f, (ccChar - 78.0f) / 42.0f);
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
        float foldN = fold * powf(fc / 246.5f, -0.163f);   // fold falls with pitch
        float lf = logf(fc / 246.5f);                      // pitch feature
        // gN (grit feature) is hoisted to the block scope above
        // sine-dip only exists when osc2 is inactive (core-only regime)
        float sineDip = 0.0f;
        if (ccChar < 64.0f) sineDip = ccChar / 64.0f;
        else if (ccChar < 85.0f) sineDip = 1.0f - (ccChar - 64.0f) / 21.0f;

        for (int m = 0; m < NM; m++)
        {
          float f = fc * (kH[m] + kK[m] * r);
          s.mfreq[m] = f;
          const float *c = kAmpFit[m];
          float a = c[0] + c[1] * dL + c[2] * foldN + c[3] * r + c[4] * lf + c[5] * gN;
          // core h3 keeps its measured sine-dip when osc2 is inactive
          if (m == 8 && r <= 0.1f) a += 0.07f * (1.0f - sineDip) - 0.07f;
          if (a < 0.0f) a = 0.0f;
          if (a > 1.5f) a = 1.5f;
          if (kGated[m])   // fold-gated core harmonics: soft presence, no clicks on sweeps
          {
            const float *gc = kGateFit[m];
            float z = gc[0] + gc[1] * dL + gc[2] * foldN + gc[3] * r + gc[4] * lf + gc[5] * gN;
            if (z < -20.0f) z = -20.0f; else if (z > 20.0f) z = 20.0f;
            a *= 1.0f / (1.0f + expf(-z));
          }

          if (kFoldRaise[m] > 0.0f)
          {
            // Intermod (k!=0) fold modes only exist once osc2 detunes them off the harmonic
            // (lattice f=fc*(h+k*r)); at r~0 they collapse onto 3f/5f and wrongly brighten,
            // with phase cancellation that also HID the real h3 boost. Their raise was fit at
            // higher shapes where they are separate, so gate it by r. On the clean shape-0
            // Character sweep this lifts h3 (0.34->0.43, HW 0.47) and h7 (0.08->0.13, HW 0.12)
            // toward hardware; higher shapes (r>0.1) are unaffected. Grid-neutral.
            float rGate = (kK[m] != 0) ? CLAMP(0.0f, 1.0f, r / 0.10f) : 1.0f;
            // Character x Shape is 2-D: a constant h3 raise nails one shape and overshoots the
            // other (HW h3 at max Character is ~0.47 at shape 0 but the mode pile-up would push
            // it to ~0.9 at high shape). The h3-family raise falls off with r to hold ~0.5-0.6
            // across the throw. Fitted slope 0.8, floored at 0.2.
            float r3g = (m == 8 || m == 11) ? CLAMP(0.2f, 1.0f, 1.0f - 0.8f * r) : 1.0f;
            a += fold * kFoldRaise[m] * rGate * r3g;
          }
          else if (kFoldKill[m] < 1.0f) a *= 1.0f - fold * (1.0f - kFoldKill[m]);
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
          // Varied (not aligned) start phases: all-aligned created an artificial
          // broadband click. Varying them matches the measured sub-mode amplitude
          // (0.23 vs HW 0.216) and punch (1.5 vs HW 1.3) while keeping the impact.
          s.phase[m] = 0.25f + 0.37f * (float)m;
          s.phase[m] -= floorf(s.phase[m]);
          float tm = tauEff * kTauR[m];
          float ceil = (kTauR[m] < 0.5f) ? hfCeilMs(f) : 1e9f;
          if (tm > ceil) tm = ceil;
          if (tm < 2.0f) tm = 2.0f;
          float Tramp = rampDurMs(tm) * 0.001f;      // ramp duration (s), tau-preserving
          s.mdecay[m] = 1.0f / (Tramp * sr);         // per-sample linear ramp decrement
        }
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

      float y = 0.0f;
      for (int m = 0; m < NM; m++)
      {
        float fm = s.mfreq[m] * pmul + jitDev;
        s.phase[m] += fm / sr; s.phase[m] -= floorf(s.phase[m]);
        if (!held) { s.ramp[m] -= s.mdecay[m]; if (s.ramp[m] < 0.0f) s.ramp[m] = 0.0f; }
        float r3 = s.ramp[m] * s.ramp[m] * s.ramp[m];   // cubed linear ramp
        y += s.env[m] * r3 * sineLUT(s.phase[m]);
      }
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
