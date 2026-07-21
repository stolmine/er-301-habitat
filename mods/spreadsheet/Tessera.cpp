#include "Tessera.h"
#include "DrumVoiceSineLUT.h"   // static half-sine LUT (no runtime trig on am335x)
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

// no-tree-vectorize is load-bearing on am335x (feedback_disable_tree_vectorize_am335x).
#pragma GCC optimize("no-tree-vectorize")

namespace stolmine
{
  // ---------------------------------------------------------------------------
  // Modal engine fitted to the Trinity BLOCK mode-structure campaign (1143 hardware
  // captures: fine 1-D throws for all 8 controls + all 28 2-D pair matrices).
  // Analysis: ~/repos/trinity-midi-harness/analysis-modemap.md
  //
  // KEY STRUCTURAL FINDING: the hardware spectrum is NOT stretched odd harmonics. Every
  // capture factorizes as f(h,k) = fc*(h + k*r): a core oscillator (odd harmonics h) cross-
  // modulated by a 2nd oscillator at fc*(1+r), giving uniformly-spaced intermod sidebands.
  // ~30% of sideband amplitude sits BELOW fc (the k=-1,-2 "sub" modes) - that is the
  // hardware's body/punch, which a stretched-harmonic model cannot produce at all.
  //
  // Two decay classes (measured): "tone" modes (carrier / osc2 / folded core harmonics)
  // ring at tauRatio ~0.93-1.0; ALL sidebands are short (~0.35, weak ones 0.15-0.2).
  // That class split IS the measured brightness-decay mechanism.
  // All control interdependencies reduce to analytic forms (no 2-D tables needed).
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
  static const float kDrive = 12.0f;
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

  struct Tessera::Internal
  {
    float phase[NM], env[NM], mfreq[NM], mdecay[NM];
    float ramp[NM];              // cubed-ramp decay state (1 -> 0), cubed at output
    float pitchEnv = 0, pitchCoeff = 0.999f, startMult = 1;
    float noiseEnv = 0, noiseCoeff = 0, noiseLp = 0, burst = 0, burstCoeff = 0;
    float jitLp = 0, jitHz = 0;          // common-mode FM state + per-hit depth
    uint32_t jitRng = 0x9e3779b9u;       // own stream: must not perturb the noise bed
    int holdLeft = 0;
    float prevTrig = 0;
    uint32_t rng = 0x51ee7u;
    Internal() { for (int m = 0; m < NM; m++) { phase[m] = 0; env[m] = 0; mfreq[m] = 0; mdecay[m] = 0; ramp[m] = 0; } }
  };

  Tessera::Tessera()
  {
    addInput(mTrigger);
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mF0);
    addParameter(mCharacter);
    addParameter(mShape);
    addParameter(mGrit);
    addParameter(mSweep);
    addParameter(mTime);
    addParameter(mHold);
    addParameter(mDecay);
    addParameter(mClipper);
    addParameter(mLevel);
    mpInternal = new Internal();
  }

  Tessera::~Tessera() { delete mpInternal; }

  float Tessera::getEnvLevel() { return mpInternal->env[0]; }

  void Tessera::process()
  {
    float *trig = mTrigger.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();
    Internal &I = *mpInternal;
    float sr = globalConfig.sampleRate;
    float nyq = sr * 0.45f;

    // knobs 0..1 -> the hardware's CC 0..127 throw (all laws fitted in CC domain)
    float ccChar = CLAMP(0.0f, 1.0f, mCharacter.value()) * 127.0f;
    float ccShape = CLAMP(0.0f, 1.0f, mShape.value()) * 127.0f;
    float ccGrit = CLAMP(0.0f, 1.0f, mGrit.value()) * 127.0f;
    float ccSweep = CLAMP(0.0f, 1.0f, mSweep.value()) * 127.0f;
    float ccTime = CLAMP(0.0f, 1.0f, mTime.value()) * 127.0f;
    float ccHold = CLAMP(0.0f, 1.0f, mHold.value()) * 127.0f;
    float ccDecay = CLAMP(0.0f, 1.0f, mDecay.value()) * 127.0f;
    float ccClip = CLAMP(0.0f, 1.0f, mClipper.value()) * 127.0f;
    float level = CLAMP(0.0f, 1.0f, mLevel.value());
    float f0 = CLAMP(8.0f, 8000.0f, mF0.value());

    // Clipper -> (threshold, makeup gain), linear interp over the measured table.
    // Normalized so the default (CC 48) is unity gain: that is the operating point the
    // whole 1143-capture corpus was recorded at, so it is the state every amplitude
    // coefficient in this file was fitted against.
    float jitCoeff = 1.0f - expf(-6.2832f * kGritLpHz / globalConfig.sampleRate);
    // noise() is uniform (std 1/sqrt(3)); the one-pole scales variance by a/(2-a).
    // Normalise both so kappa*fc IS the resulting deviation in Hz.
    float jitNorm = 1.7320508f * kGritDepthTrim / sqrtf(jitCoeff / (2.0f - jitCoeff));
    // No attack ramp: the firmware envelope jumps to 1.0 in one instruction (its ramp
    // path is dead code, rate constant exactly 0.0). The old 2 ms one-pole here was also
    // provably inert on the shipped unit - the 12x pre-clipper drive limits the onset to
    // the same ceiling with or without it (verified byte-identical on the grid), so it had
    // done nothing since the drive was adopted. Removed: matches hardware, simpler loop.
    float clipTh, clipG;
    {
      float u = CLAMP(0.0f, 15.0f, ccClip / 8.0f);
      int ci = (int)u;
      if (ci > 14) ci = 14;
      float cf = u - (float)ci;
      clipTh = (kClipTh[ci] + (kClipTh[ci + 1] - kClipTh[ci]) * cf) / kSMH;
      clipG = (kClipG[ci] + (kClipG[ci + 1] - kClipG[ci]) * cf) / 3.329f;
    }

    // Shape -> osc2 detune ratio (measured linear, pitch-tracked)
    float r = CLAMP(0.0f, 2.0f, 0.0189f * (ccShape - 9.2f));
    // Character -> fold amount: dead zone to CC 78, then linear (when osc2 active)
    float fold = CLAMP(0.0f, 1.0f, (ccChar - 78.0f) / 42.0f);
    // Decay -> carrier tau (quadratic-in-log law, ~10 ms .. 3 s)
    float tauC = expf(2.447f + 0.0576f * ccDecay - 0.000115f * ccDecay * ccDecay);  // ms
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

    // Sweep -> start pitch multiplier (linear in OCTAVES); Time -> pitch-env tau (exp)
    float startMult = 1.12f * powf(2.0f, ccSweep / 22.5f);
    float tauP = 0.002f * expf(0.042f * ccTime);
    float pitchCoeff = expf(-1.0f / (tauP * sr));
    // Hold -> plateau (exponential), clamp 4 s
    float holdSec = 0.001f * powf(2.0f, ccHold / 8.6f);
    if (holdSec > 4.0f) holdSec = 4.0f;
    int holdSamples = (int)(holdSec * sr);
    float noiseLpG = 1.0f - expf(-6.2832f * 4000.0f / sr);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float tv = trig[i];
      if (tv > 0.5f && I.prevTrig <= 0.5f)   // rising edge -> new hit
      {
        float fc = f0 * powf(2.0f, voct[i]);
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
          I.mfreq[m] = f;
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
          I.env[m] = a;               // static initial amplitude; the ramp carries the decay
          I.ramp[m] = 1.0f;
          // Varied (not aligned) start phases: all-aligned created an artificial
          // broadband click. Varying them matches the measured sub-mode amplitude
          // (0.23 vs HW 0.216) and punch (1.5 vs HW 1.3) while keeping the impact.
          I.phase[m] = 0.25f + 0.37f * (float)m;
          I.phase[m] -= floorf(I.phase[m]);
          float tm = tauEff * kTauR[m];
          float ceil = (kTauR[m] < 0.5f) ? hfCeilMs(f) : 1e9f;
          if (tm > ceil) tm = ceil;
          if (tm < 2.0f) tm = 2.0f;
          float Tramp = rampDurMs(tm) * 0.001f;      // ramp duration (s), tau-preserving
          I.mdecay[m] = 1.0f / (Tramp * sr);         // per-sample linear ramp decrement
        }
        I.startMult = startMult;
        I.pitchCoeff = pitchCoeff;
        I.pitchEnv = 1.0f;
        I.holdLeft = holdSamples;
        I.noiseEnv = 1.0f;
        I.noiseCoeff = expf(-1.0f / (noiseTau * 0.001f * sr));
        // regime-4 attack burst (measured punch 9.8 / atk_frac 0.41 at grit >= ~115)
        I.burst = (ccGrit >= 115.0f) ? 8.0f : 0.0f;
        I.burstCoeff = expf(-1.0f / (0.005f * sr));
        {
          float u = CLAMP(0.0f, 15.0f, ccGrit / 8.0f);
          int gi = (int)u; if (gi > 14) gi = 14;
          float gf = u - (float)gi;
          I.jitHz = (kGritKappa[gi] + (kGritKappa[gi + 1] - kGritKappa[gi]) * gf) * fc;
        }
      }
      I.prevTrig = tv;

      I.pitchEnv *= I.pitchCoeff;
      float pmul = 1.0f + (I.startMult - 1.0f) * I.pitchEnv;
      bool held = I.holdLeft > 0;

      // one shared noise-FM deviation in Hz, added identically to every mode
      I.jitLp += jitCoeff * (noise(I.jitRng) - I.jitLp);
      float jitDev = I.jitLp * jitNorm * I.jitHz;

      float y = 0.0f;
      for (int m = 0; m < NM; m++)
      {
        float fm = I.mfreq[m] * pmul + jitDev;
        I.phase[m] += fm / sr; I.phase[m] -= floorf(I.phase[m]);
        if (!held) { I.ramp[m] -= I.mdecay[m]; if (I.ramp[m] < 0.0f) I.ramp[m] = 0.0f; }
        float r3 = I.ramp[m] * I.ramp[m] * I.ramp[m];   // cubed linear ramp
        y += I.env[m] * r3 * sineLUT(I.phase[m]);
      }
      if (held) I.holdLeft--;

      // noise body (own env) + grit attack burst
      I.noiseEnv *= I.noiseCoeff;
      I.burst *= I.burstCoeff;
      I.noiseLp += noiseLpG * (noise(I.rng) - I.noiseLp);
      y = y * (1.0f - noiseMix * 0.5f) + I.noiseLp * (noiseMix * I.noiseEnv + I.burst * 0.12f);

      float yd = y * kDrive;
      float ct = yd / clipTh;
      // __builtin_sqrtf maps straight to VFP vsqrt.f32. Plain sqrtf() left GCC emitting
      // an out-of-line `bl sqrtf` fallback in the per-sample loop, which is both slow on
      // Cortex-A8 and an AAPCS call barrier inside the audio path. 1+ct*ct is provably
      // >= 1, so the fallback is dead weight.
      out[i] = clipG * yd / __builtin_sqrtf(1.0f + ct * ct) * level;
    }
  }

} // namespace stolmine
