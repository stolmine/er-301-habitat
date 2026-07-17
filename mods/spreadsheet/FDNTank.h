#pragma once

// FDNTank -- a feedback-delay-network reverb core (working name; see
// planning/fdn-reverb-design.md). 8 delay lines, a lossless Householder
// feedback matrix, per-line frequency-dependent loss filters (Phase 2:
// RT60-based gain + HF damping + bass low-shelf = the "spectral RT60"),
// modulated fractional delay reads (Phase 2d: per-line slow LFOs
// de-metalize the tail, base-delay slew gives Doppler-smooth Size
// sweeps), a 4-stage Schroeder input diffuser, an equal-power dry/wet
// crossfade, and a Weave control that morphs the feedback matrix through
// a butterfly network of 2x2 rotations (isolated lines -> coupled pairs
// -> coupled quads -> full Hadamard wash), traversing reverb
// architectures -- the Erbe-Verb alpha-matrix idea, always orthonormal
// so stable + energy-preserving at every setting.
// Internal-stereo (one shared tank,
// decorrelated L/R output taps) so the Lua wiring just maps In1/In2 ->
// In L/In R and Out L/Out R -> Out1/Out2 (the Fabula pattern).
//
// The point of this topology (vs Fabula's Dattorro tank) is the NEON
// ceiling: fixed / block-modulated delay lengths mean CONTIGUOUS reads
// at a moving write head (not per-sample gathers), and the matrix is a
// reduce + broadcast-subtract. The DSP stays SCALAR and am335x-safe
// through the Phase-2 voicing so we can hear it and A/B the tail; the
// NEON pass (matrix + loss-filter bank + output dot-products) is Phase 4,
// once the voicing is settled. See feedback_neon_soa_svf_bank /
// Visadhara.h for the vectorization template.
//
// All virtuals defined inline in this header per
// feedback_no_out_of_line_virtuals (COMDAT vtable, immune to
// firmware/package vtable drift). No FDNTank.cpp.

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

namespace stolmine
{
  // ---- Topology constants -------------------------------------------------

  static const int kFdnLines = 8;

  // Per-line ring buffers. Power-of-2 so the read index is a mask, and
  // sized to hold the longest line at up to ~96 kHz (kFdnBaseMs max
  // ~69 ms -> 6.6 k samples at 96 k) with headroom. One shared write
  // index advances once per sample; line i reads (write - L[i]) & mask,
  // which is contiguous (the NEON crux vs Fabula's gathers).
  static const int kFdnLineBufLen = 8192;
  static const int kFdnLineMask   = kFdnLineBufLen - 1;

  // Input diffuser (4 series Schroeder allpasses). Backing arrays are
  // max-sized; the live circular length is the runtime delay (samples).
  static const int kFdnDiffBufLen = 2048;

  // Base line delays in ms. Mutually near-coprime (no shared rational
  // ratios -> no reinforced eigentones); span ~1.5 ms:1 for density.
  // Scaled by Size (block rate) into integer sample lengths.
  static const float kFdnBaseMs[kFdnLines] = {
    21.5f, 28.4f, 35.3f, 41.6f, 48.1f, 55.2f, 62.4f, 69.1f
  };

  // Diffuser stage delays (ms) + gain. Short primes, phase-scramble the
  // input so the tank isn't sparse/metallic before the lines fill.
  static const float kFdnDiffMs[4] = { 3.0f, 5.2f, 7.9f, 11.3f };
  static const float kFdnDiffG     = 0.5f;

  // Injection pattern: a balanced +/-1 vector (sum 0) so the diffused
  // input enters the 8 lines decorrelated rather than as one coherent
  // spike. Distinct from the two output vectors below.
  static const float kFdnInj[kFdnLines] = {
    1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f
  };

  // Two further balanced +/-1 vectors tap the lines into L / R, giving a
  // decorrelated stereo wet from the single shared tank.
  static const float kFdnOutL[kFdnLines] = {
    1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f
  };
  static const float kFdnOutR[kFdnLines] = {
    1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, 1.0f
  };

  static const float kFdnInGain   = 0.5f;           // injection level
  static const float kFdnOutNorm  = 0.35355339f;    // 1/sqrt(8), tap sum norm
  static const float kFdnWetMakeup = 1.2f;          // voiced-by-ear wet trim

  // DC blocker on the diffused feed (one-pole HPF, ~50 Hz at 48 kHz).
  // The lossless matrix + one-pole damping both pass DC, so block it on
  // the way in rather than let a slow offset circulate.
  static const float kFdnDcR = 0.9935f;

  // ---- Phase-2 decay: frequency-dependent RT60 -------------------------------
  // Decay maps log-spaced to a mid-band RT60 in seconds; each line takes a
  // per-loop gain g_i = exp(kFdnNeg3Ln10 * T_i / RT60) so every line reaches
  // -60 dB at the same wall-clock time (a uniform, tuned tail). The matrix
  // stays lossless; all decay lives in the per-line loss filters.
  static const float kFdnRt60Min   = 0.2f;         // s at Decay=0
  static const float kFdnRt60Max   = 30.0f;        // s at Decay=1
  static const float kFdnNeg3Ln10  = -6.9077553f;  // -3*ln(10)
  static const float kFdnGainCeil   = 0.9995f;      // per-loop gain clamp
  static const float kFdnBassHz     = 300.0f;       // low-shelf corner (one-pole)

  // ---- Phase-2d modulation + fractional reads --------------------------------
  // Per-line slow LFOs wobble the delay lengths to break the fixed
  // eigenfrequencies (de-metalize sustained tails); fractional (linearly
  // interpolated) reads make the moving delay continuous and also smooth Size
  // sweeps. Depth is a small fraction of each line's length (chorus-subtle).
  static const float kFdnModDepthFrac = 0.0025f;    // +/- 0.25% of line length
  static const float kFdnBaseSlewTau  = 0.030f;     // s, Size-sweep glide (Doppler)
  // Decorrelated slow LFO rates (Hz), mutually non-harmonic.
  static const float kFdnLfoHz[kFdnLines] = {
    0.37f, 0.48f, 0.59f, 0.71f, 0.83f, 0.95f, 1.07f, 1.19f
  };
  // Init points spread around the unit circle (8 * 2pi/8) so the per-line
  // magic-circle LFOs start at decorrelated phases -- no runtime trig needed
  // (avoids the package sinf/cosf hazard, feedback_package_trig_lut).
  static const float kFdnLfoX0[kFdnLines] = {
    1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f
  };
  static const float kFdnLfoY0[kFdnLines] = {
    0.0f, 0.70710678f, 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f
  };

  // ---- Weave: feedback-matrix architecture morph -----------------------------
  // The 8x8 feedback matrix is a 3-stage butterfly network of 2x2 rotations
  // (the FWHT structure). Opening the stages progressively grows the mixing
  // blocks: identity -> coupled pairs -> coupled quads -> full Hadamard wash.
  // A product of rotations is orthonormal at EVERY setting -> stable and
  // energy-preserving across the whole Weave sweep. Weave maps to three stage
  // "openness" values (p0/p1/p2) that ramp in sequence; angle = p * pi/4.
  static const float kFdnQuarterPi = 0.7853981634f;  // 45 deg = full butterfly

  // Polynomial cos/sin on [0, pi/4] (Taylor, ~1e-5) so no runtime sinf/cosf
  // (feedback_package_trig_lut); used at BLOCK rate for the 3 stage angles.
  static inline float fdnCosApprox(float x)
  {
    const float x2 = x * x;
    return 1.0f + x2 * (-0.5f + x2 * 0.04166667f);
  }
  static inline float fdnSinApprox(float x)
  {
    const float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * 0.00833333f));
  }

  // Cubic soft clipper (sin-free, Spiral-flavored warmth): unity slope at the
  // origin so it can't boost small-signal loop gain (stable), a smooth knee at
  // +/-1, hard-bounded to +/-2/3. Drive is applied OUTSIDE and divided back out
  // (sat(drive*x)/drive) so higher drive = harder saturation WITHOUT changing
  // the small-signal loop gain. Also serves as the feedback blow-up guard.
  static inline float fdnSoftSat(float x)
  {
    if (x > 1.0f) return 0.66666667f;
    if (x < -1.0f) return -0.66666667f;
    return x - x * x * x * 0.33333333f;
  }

  // Cheap tan approximation for the TPT-SVF prewarp g = tan(pi*fc/SR) over the
  // audio range (arg < ~0.6 rad): x + x^3/3 + 2x^5/15. No runtime tanf/sinf
  // (feedback_package_trig_lut); block-rate.
  static inline float fdnTanApprox(float x)
  {
    const float x2 = x * x;
    return x * (1.0f + x2 * (0.33333333f + x2 * 0.13333333f));
  }

  // ---- Wooden-body resonator (WoodenBox-flavored) ----------------------------
  // An inharmonic modal bank (TPT-SVF bandpasses) rung by the wet and mixed in
  // only at the sparse (low-Weave) end, so the isolated resonators pick up a
  // hollow wooden body. This captures WoodenBox's resonant-body essence cheaply
  // in-atom (the real WoodenBox is a full double FDN reverb -- too heavy to nest).
  static const int kFdnModes = 5;
  static const float kFdnModeHz[kFdnModes] = {
    180.0f, 296.0f, 471.0f, 683.0f, 1029.0f   // inharmonic ~1:1.6:2.6:3.8:5.7
  };
  static const float kFdnModeGain[kFdnModes] = {
    1.0f, 0.8f, 0.6f, 0.45f, 0.3f             // lower modes carry the body
  };
  static const float kFdnModeDamp  = 0.167f;  // 1/Q, Q~6 (woody, not ringy)
  static const float kFdnBodyDepth = 0.06f;   // body level at Weave=0 (by ear)

  // ---- Wardrobe: aggregate-driven bold coloration on the wet -----------------
  // A mono series chain (wavefold -> bitcrush -> ring-mod -> comb) whose params
  // are driven by AGGREGATES of the whole control state, not one knob:
  //   mass     = 1/2(Size + Decay)      -- bigness
  //   ferocity = Decay * (1 - Weave)    -- long + sparse = hot resonators
  //   bright   = 1/2 + Bass - Damp      -- dark <-> bright
  // The added color (chain - mono) is scaled by wardPresence (~0 at the default
  // clean wash, rising in the resonant zone) and mixed into BOTH wet channels,
  // so the clean stereo is preserved and centered color rides on top.
  static const int kFdnCombBufLen = 512;
  static const int kFdnCombMask   = kFdnCombBufLen - 1;
  static const float kFdnRmHzMin = 50.0f;
  static const float kFdnRmHzMax = 800.0f;

  // Directional activation ramp: 0 at/below lo, 1 at/above hi (calm end off).
  static inline float fdnRamp(float x, float lo, float invSpan)
  {
    float y = (x - lo) * invSpan;
    return y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
  }

  // Triangle wavefolder (stateless, 3 reflections; no trig): folds signal back
  // into [-1,1] for complex harmonics as drive pushes it past the rails.
  static inline float fdnFold(float x)
  {
    for (int r = 0; r < 3; r++)
    {
      if (x > 1.0f) x = 2.0f - x;
      else if (x < -1.0f) x = -2.0f - x;
    }
    return x;
  }

  // Proven Schroeder allpass step (carbon copy of Network's
  // networkAllpassStep): v = in + g*buf; out = -g*v + buf; buf = v.
  // Phase-scrambles, magnitude spectrum unchanged. buf is a circular
  // buffer of live length N.
  static inline float fdnAllpassStep(float in, float *buf, int N, int &idx, float g)
  {
    const float bufVal = buf[idx];
    const float v = in + g * bufVal;
    const float out = -g * v + bufVal;
    buf[idx] = v;
    idx++;
    if (idx >= N) idx = 0;
    return out;
  }

  class FDNTank : public od::Object
  {
  public:
    FDNTank()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mSize);
      addParameter(mDecay);
      addParameter(mBass);
      addParameter(mDamp);
      addParameter(mMix);
      addParameter(mWeave);

      memset(mLine, 0, sizeof(mLine));
      memset(mDiff, 0, sizeof(mDiff));
      memset(mHfLp, 0, sizeof(mHfLp));
      memset(mBassLp, 0, sizeof(mBassLp));
      memset(mBody1, 0, sizeof(mBody1));
      memset(mBody2, 0, sizeof(mBody2));
      mCrushPhase = 0.0f;
      mCrushHold = 0.0f;
      mMuffle = 0.0f;
      mRmX = 1.0f;   // ring-mod carrier magic-circle init (phase 0)
      mRmY = 0.0f;
      memset(mComb, 0, sizeof(mComb));
      mCombW = 0;
      for (int a = 0; a < 4; a++) mDiffIdx[a] = 0;
      for (int i = 0; i < kFdnLines; i++)
      {
        mBaseDelay[i] = 0.0f;         // primed to target on first process()
        mLfoX[i] = kFdnLfoX0[i];      // decorrelated LFO start phases
        mLfoY[i] = kFdnLfoY0[i];
      }
      mPrimed = false;
      mWrite = 0;
      mDcX1 = 0.0f;
      mDcY1 = 0.0f;
    }

    virtual ~FDNTank() {}

    // Everything below is hidden from the SWIG parser (SWIGLUA is defined
    // during %include) and seen only by the C++ compiler, which parses
    // the header with SWIGLUA undefined (the %{ #undef SWIGLUA %} block)
    // so `new FDNTank()` allocates the full object. This is the
    // Network/APFTank convention (feedback_swig_header_dep).
#ifndef SWIGLUA
    virtual void process()
    {
      const float *inL = mInL.buffer();
      const float *inR = mInR.buffer();
      float *outL = mOutL.buffer();
      float *outR = mOutR.buffer();

      // ---- Block-rate parameter reads + clamps ----
      float sizeN = mSize.value();
      if (!(sizeN >= 0.0f)) sizeN = 0.0f;
      if (sizeN > 1.0f) sizeN = 1.0f;

      float decayN = mDecay.value();
      if (!(decayN >= 0.0f)) decayN = 0.0f;
      if (decayN > 1.0f) decayN = 1.0f;

      float bassN = mBass.value();
      if (!(bassN >= 0.0f)) bassN = 0.0f;
      if (bassN > 1.0f) bassN = 1.0f;

      float dampN = mDamp.value();
      if (!(dampN >= 0.0f)) dampN = 0.0f;
      if (dampN > 1.0f) dampN = 1.0f;

      float mixN = mMix.value();
      if (!(mixN >= 0.0f)) mixN = 0.0f;
      if (mixN > 1.0f) mixN = 1.0f;

      // Weave: feedback-matrix architecture morph via a 3-stage butterfly.
      // 0 = identity (isolated lines / sparse echoes); ~1/3 = coupled pairs;
      // ~2/3 = coupled quads; 1 = full Hadamard wash. Progressive stage
      // openness p0/p1/p2, angle = p*pi/4; cos/sin at block rate.
      float warpN = mWeave.value();
      if (!(warpN >= 0.0f)) warpN = 0.0f;
      if (warpN > 1.0f) warpN = 1.0f;
      float p0 = warpN * 3.0f;
      if (p0 > 1.0f) p0 = 1.0f;
      float p1 = (warpN - 0.33333333f) * 3.0f;
      if (p1 < 0.0f) p1 = 0.0f; else if (p1 > 1.0f) p1 = 1.0f;
      float p2 = (warpN - 0.66666667f) * 3.0f;
      if (p2 < 0.0f) p2 = 0.0f; else if (p2 > 1.0f) p2 = 1.0f;
      const float c0 = fdnCosApprox(p0 * kFdnQuarterPi);
      const float sN0 = fdnSinApprox(p0 * kFdnQuarterPi);
      const float c1 = fdnCosApprox(p1 * kFdnQuarterPi);
      const float sN1 = fdnSinApprox(p1 * kFdnQuarterPi);
      const float c2 = fdnCosApprox(p2 * kFdnQuarterPi);
      const float sN2 = fdnSinApprox(p2 * kFdnQuarterPi);

      // In-loop saturation drive, tied to Weave: driven/gnarly at the sparse
      // (isolated-resonator) end, clean at the dense wash end. sat(drive*x)/drive
      // keeps small-signal gain unity (stable) while raising saturation hardness.
      const float satDrive = 1.0f + (1.0f - warpN) * 2.0f;   // 3 (sparse) .. 1 (wash)
      const float invSatDrive = 1.0f / satDrive;

      const float invSR = 1.0f / globalConfig.sampleRate;

      // Size -> target base delays (float samples, block rate). 0.1..1.0 of
      // the base delays; leave 2 samples of headroom above the min and below
      // the ring end for the +1 interpolation tap and the LFO swing.
      const float sizeScale = 0.1f + 0.9f * sizeN;
      const float delayMax = (float)(kFdnLineMask - 2);
      float targetLf[kFdnLines];
      for (int i = 0; i < kFdnLines; i++)
      {
        float lf = kFdnBaseMs[i] * 0.001f * globalConfig.sampleRate * sizeScale;
        if (lf < 4.0f) lf = 4.0f;
        if (lf > delayMax) lf = delayMax;
        targetLf[i] = lf;
      }
      // Prime the smoothed base delays to target on the first block so Size
      // doesn't glide up from 0 at insert (a startup swoop).
      if (!mPrimed)
      {
        for (int i = 0; i < kFdnLines; i++) mBaseDelay[i] = targetLf[i];
        mPrimed = true;
      }
      // Per-sample base-delay slew coefficient (one-pole ~30 ms) -> Size
      // sweeps glide (Doppler) instead of zippering. Per-line LFO increment
      // (small-angle magic-circle: eps = 2*pi*f/SR) + proportional mod depth.
      const float slewCoef =
        1.0f - expf(-1.0f / (kFdnBaseSlewTau * globalConfig.sampleRate));
      float lfoEps[kFdnLines];
      float modDepth[kFdnLines];
      for (int i = 0; i < kFdnLines; i++)
      {
        lfoEps[i] = 6.2831853f * invSR * kFdnLfoHz[i];
        modDepth[i] = kFdnModDepthFrac * mBaseDelay[i];
      }

      // Diffuser stage lengths (constant given fixed SR; recomputed each
      // block is trivial).
      int diffLen[4];
      for (int a = 0; a < 4; a++)
      {
        int l = (int)(kFdnDiffMs[a] * 0.001f * globalConfig.sampleRate);
        if (l < 1) l = 1;
        if (l > kFdnDiffBufLen) l = kFdnDiffBufLen;
        diffLen[a] = l;
      }

      // ---- Phase-2 frequency-dependent decay (block rate) ----
      // Decay -> mid RT60, log-spaced so the knob travel is musical.
      const float rt60Mid = kFdnRt60Min * powf(kFdnRt60Max / kFdnRt60Min, decayN);
      // Bass -> bass-RT60 ratio (0.5 = neutral). 2^((bass-0.5)*2) ~ 0.25x..4x:
      // bass rings shorter (<0.5) or longer / boomier (>0.5).
      const float bassRatio = powf(2.0f, (bassN - 0.5f) * 2.0f);
      const float rt60Bass = rt60Mid * bassRatio;

      // Per-line loss gains: mid g_i and bass-band g_bass_i, both RT60 gains
      // clamped < 1 for stability. r_i = g_bass_i / g_i is the low-shelf ratio.
      // 8 expf pairs at BLOCK rate (scalar expf is am335x-safe -- Network uses
      // it; the per-sample loop below stays transcendental-free).
      float gLine[kFdnLines];
      float rBass[kFdnLines];
      for (int i = 0; i < kFdnLines; i++)
      {
        const float Ti = targetLf[i] * invSR;   // line delay, seconds
        float gi = expf(kFdnNeg3Ln10 * Ti / rt60Mid);
        if (gi > kFdnGainCeil) gi = kFdnGainCeil;
        float gb = expf(kFdnNeg3Ln10 * Ti / rt60Bass);
        if (gb > kFdnGainCeil) gb = kFdnGainCeil;
        gLine[i] = gi;
        rBass[i] = gb / gi;
      }

      // Damp -> HF one-pole coefficient (0 = flat, up to ~0.7 = dark). The
      // loss filter is g_i * one-pole-LP: DC gain g_i, HF gain
      // g_i*(1-hf)/(1+hf) -> highs decay faster than mids.
      const float hfCoef = 0.7f * dampN;
      // Bass-band one-pole corner (~300 Hz), a = exp(-2*pi*fc/SR).
      const float bassCoef = expf(-6.2831853f * kFdnBassHz * invSR);

      // Equal-power (sqrt-law) dry/wet: the wet is decorrelated from the
      // dry, so a linear crossfade would dip ~3 dB at center. This is the
      // house standard (feedback_equal_power_drywet_crossfade).
      const float dryG = sqrtf(1.0f - mixN);
      const float wetG = sqrtf(mixN);

      // Wooden-body modal SVF coefficients (block rate) + Weave-tied mix: the
      // body fades in toward the sparse end (bodyMix = (1-Weave)*depth).
      float bodyA1[kFdnModes], bodyA2[kFdnModes], bodyA3[kFdnModes];
      for (int m = 0; m < kFdnModes; m++)
      {
        const float g = fdnTanApprox(3.14159265f * kFdnModeHz[m] * invSR);
        const float a1 = 1.0f / (1.0f + g * (g + kFdnModeDamp));
        bodyA1[m] = a1;
        bodyA2[m] = g * a1;
        bodyA3[m] = g * bodyA2[m];
      }
      const float bodyMix = (1.0f - warpN) * kFdnBodyDepth;

      // ---- Wardrobe: ORTHOGONAL aggregate mapping (verified spread) ----
      // Each effect is driven by a distinct control via a directional ramp
      // (calm end = off), so the six effects decorrelate (measured |corr| 0.08
      // vs 0.95 for the old ferocity-gated design) and "drama" spreads evenly
      // across the control space with a reachable clean corner. No global gate:
      // at the calm settings every effect is at identity -> clean.
      const float mass = 0.5f * (sizeN + decayN);
      const float ferocity = decayN * (1.0f - warpN);
      float bright = 0.5f + bassN - dampN;
      if (bright < 0.0f) bright = 0.0f; else if (bright > 1.0f) bright = 1.0f;

      const float cFold   = fdnRamp(decayN, 0.45f, 1.0f / 0.55f);       // Decay
      const float cCrush  = fdnRamp(sizeN, 0.40f, 1.0f / 0.60f);        // Size
      const float cRing   = fdnRamp(1.0f - warpN, 0.45f, 1.0f / 0.55f); // Weave (sparse)
      const float cMuffle = fdnRamp(dampN, 0.45f, 1.0f / 0.55f);        // Damp
      const float cDrive  = fdnRamp(bassN, 0.50f, 1.0f / 0.50f);        // Bass
      const float cComb   = fdnRamp(ferocity, 0.12f, 1.0f / 0.68f);     // long+sparse

      const float foldDrive = 1.0f + cFold * 4.0f;
      // Bitcrush/decimate -- AGGRESSIVE: rate down to ~0.03 (SR/33 ~ 1.4 kHz)
      // and step up to 0.4 (~2-3 bit).
      const float crushRate = 1.0f - cCrush * 0.97f;
      const float crushStep = 0.002f + cCrush * 0.4f;
      const float invCrushStep = 1.0f / crushStep;
      const float rmEps =
        6.2831853f * (kFdnRmHzMin + bright * (kFdnRmHzMax - kFdnRmHzMin)) * invSR;
      const float rmAmt = cRing * 0.8f;
      const float muffleK = 1.0f - cMuffle * 0.9f;   // 1 = open, 0.1 = muffled
      const float driveAmt = cDrive;
      int combLen = (int)(40.0f + mass * 400.0f);    // tuning from mass
      if (combLen > kFdnCombMask) combLen = kFdnCombMask;
      const float combFb = decayN * 0.85f;
      const float combMix = cComb * 0.7f;

      for (int n = 0; n < FRAMELENGTH; n++)
      {
        const float dryL = inL[n];
        const float dryR = inR[n];

        // Mono downmix feed + DC blocker (y = x - x1 + R*y1).
        float mono = 0.5f * (dryL + dryR);
        const float hp = mono - mDcX1 + kFdnDcR * mDcY1;
        mDcX1 = mono;
        mDcY1 = hp;

        // Input diffuser (4 series allpasses).
        float x = hp;
        x = fdnAllpassStep(x, mDiff[0], diffLen[0], mDiffIdx[0], kFdnDiffG);
        x = fdnAllpassStep(x, mDiff[1], diffLen[1], mDiffIdx[1], kFdnDiffG);
        x = fdnAllpassStep(x, mDiff[2], diffLen[2], mDiffIdx[2], kFdnDiffG);
        x = fdnAllpassStep(x, mDiff[3], diffLen[3], mDiffIdx[3], kFdnDiffG);
        const float inject = kFdnInGain * x;

        // Read the 8 delayed line outputs at a MODULATED FRACTIONAL delay.
        // Per line: slew the base delay toward its Size target (Doppler
        // glide), add a slow magic-circle LFO (de-metalizes the tail), then
        // linearly interpolate between the two straddling samples. The read
        // is still a local 2-tap (contiguous), not a scattered gather, so it
        // stays NEON-friendly for Phase 4.
        float d[kFdnLines];
        for (int i = 0; i < kFdnLines; i++)
        {
          mBaseDelay[i] += slewCoef * (targetLf[i] - mBaseDelay[i]);

          // Magic-circle LFO (area-preserving, amplitude-stable, no trig).
          mLfoX[i] += lfoEps[i] * mLfoY[i];
          mLfoY[i] -= lfoEps[i] * mLfoX[i];

          float Df = mBaseDelay[i] + modDepth[i] * mLfoX[i];
          if (Df < 4.0f) Df = 4.0f;
          else if (Df > delayMax) Df = delayMax;

          const int Di = (int)Df;
          const float frac = Df - (float)Di;
          const float s0 = mLine[i][(mWrite - Di) & kFdnLineMask];
          const float s1 = mLine[i][(mWrite - Di - 1) & kFdnLineMask];
          d[i] = s0 + frac * (s1 - s0);
        }

        // Per-line loss filter (Phase 2): gain g_i + HF one-pole damping +
        // bass low-shelf -> frequency-dependent RT60. SoA one-pole states
        // (the future NEON bank). d[] stays RAW for the brighter wet taps;
        // the loss-filtered signal feeds the lossless matrix.
        float loss[kFdnLines];
        for (int i = 0; i < kFdnLines; i++)
        {
          // HF damping one-pole (DC gain 1): lp = (1-hf)*d + hf*lp.
          mHfLp[i] += (1.0f - hfCoef) * (d[i] - mHfLp[i]);
          const float base = gLine[i] * mHfLp[i];   // DC -> g_i, HF -> g_i*less
          // Bass low-shelf toward g_bass at DC (r_i = g_bass/g_i, monotonic
          // first-order shelf so no overshoot -> stays < 1).
          mBassLp[i] += (1.0f - bassCoef) * (base - mBassLp[i]);
          loss[i] = base + (rBass[i] - 1.0f) * mBassLp[i];
        }

        // Feedback matrix = Weave butterfly (3-stage FWHT of 2x2 rotations):
        // stage 0 mixes pairs (i,i^1), stage 1 (i,i^2), stage 2 (i,i^4). The
        // per-stage angle opens with Weave -> identity -> pairs -> quads ->
        // Hadamard. Product of rotations = orthonormal at every angle, so the
        // loop stays stable and energy-preserving; decay is set by loss[].
        float t[kFdnLines];
        for (int i = 0; i < kFdnLines; i++) t[i] = loss[i];
        for (int i = 0; i < kFdnLines; i += 2)   // stage 0: (0,1)(2,3)(4,5)(6,7)
        {
          const float a = t[i], b = t[i + 1];
          t[i]     = c0 * a - sN0 * b;
          t[i + 1] = sN0 * a + c0 * b;
        }
        for (int i = 0; i < kFdnLines; i += 4)   // stage 1: (0,2)(1,3)(4,6)(5,7)
          for (int k = 0; k < 2; k++)
          {
            const int p = i + k, q = i + k + 2;
            const float a = t[p], b = t[q];
            t[p] = c1 * a - sN1 * b;
            t[q] = sN1 * a + c1 * b;
          }
        for (int k = 0; k < 4; k++)              // stage 2: (0,4)(1,5)(2,6)(3,7)
        {
          const int p = k, q = k + 4;
          const float a = t[p], b = t[q];
          t[p] = c2 * a - sN2 * b;
          t[q] = sN2 * a + c2 * b;
        }

        // Write feedback + injection; accumulate the stereo wet taps.
        float wetL = 0.0f;
        float wetR = 0.0f;
        for (int i = 0; i < kFdnLines; i++)
        {
          // In-loop soft saturation (also the blow-up guard: output bounded to
          // +/-2/3*invSatDrive < 1). Warmth compounds every recirculation.
          const float fb = fdnSoftSat(satDrive * t[i]) * invSatDrive;

          mLine[i][mWrite] = inject * kFdnInj[i] + fb;

          wetL += d[i] * kFdnOutL[i];
          wetR += d[i] * kFdnOutR[i];
        }
        mWrite = (mWrite + 1) & kFdnLineMask;

        wetL *= kFdnOutNorm * kFdnWetMakeup;
        wetR *= kFdnOutNorm * kFdnWetMakeup;

        // Wooden-body resonator: an inharmonic modal bank rung by the wet, added
        // back scaled by bodyMix (present only at the sparse end). Branchless
        // (always run, scaled by bodyMix) per feedback_runtime_branched_dsp.
        const float bodyIn = 0.5f * (wetL + wetR);
        float body = 0.0f;
        for (int m = 0; m < kFdnModes; m++)
        {
          const float v3 = bodyIn - mBody2[m];
          const float v1 = bodyA1[m] * mBody1[m] + bodyA2[m] * v3;   // bandpass
          const float v2 = mBody2[m] + bodyA2[m] * mBody1[m] + bodyA3[m] * v3;
          mBody1[m] = 2.0f * v1 - mBody1[m];
          mBody2[m] = 2.0f * v2 - mBody2[m];
          body += kFdnModeGain[m] * v1;
        }
        body *= bodyMix;
        wetL += body;
        wetR += body;

        // Wardrobe: orthogonal aggregate-driven mono chain (fold -> crush ->
        // ring-mod -> muffle -> drive -> comb), added as centered color (stereo
        // kept). Each effect is at identity when its activation is 0, so the
        // calm corner passes clean with no global gate.
        const float wetMono = 0.5f * (wetL + wetR);
        float w = fdnFold(wetMono * foldDrive);           // wavefold + drive
        mCrushPhase += crushRate;                         // bitcrush / decimate
        const float doHold = mCrushPhase >= 1.0f ? 1.0f : 0.0f;
        mCrushPhase -= doHold;
        const float q = crushStep * (float)(int)(w * invCrushStep);
        mCrushHold += doHold * (q - mCrushHold);
        w = mCrushHold;
        mRmX += rmEps * mRmY;                             // ring-mod (AM by carrier)
        mRmY -= rmEps * mRmX;
        w = w * (1.0f - rmAmt + rmAmt * mRmX);
        mMuffle += muffleK * (w - mMuffle);               // muffle (one-pole LP)
        w = mMuffle;
        const float biased =                              // asymmetric drive (weight)
          fdnSoftSat(w + driveAmt * 0.7f) - fdnSoftSat(driveAmt * 0.7f);
        w = w + driveAmt * (biased - w);
        const int rd = (mCombW - combLen) & kFdnCombMask; // comb resonator
        const float delayed = mComb[rd];
        mComb[mCombW] = w + combFb * delayed;
        mCombW = (mCombW + 1) & kFdnCombMask;
        w = w + combMix * delayed;
        const float colorDelta = w - wetMono;
        wetL += colorDelta;
        wetR += colorDelta;

        outL[n] = dryL * dryG + wetL * wetG;
        outR[n] = dryR * dryG + wetR * wetG;
      }
    }

    od::Inlet mInL{"In L"};
    od::Inlet mInR{"In R"};
    od::Outlet mOutL{"Out L"};
    od::Outlet mOutR{"Out R"};

    od::Parameter mSize{"Size", 0.5f};    // 0..1, line-length scale (room size)
    od::Parameter mDecay{"Decay", 0.6f};  // 0..1, mid RT60 (log-spaced)
    od::Parameter mBass{"Bass", 0.5f};    // 0..1, bass-decay ratio (0.5 = neutral)
    od::Parameter mDamp{"Damp", 0.3f};    // 0..1, HF damping (dark tail)
    od::Parameter mMix{"Mix", 0.35f};     // 0..1, equal-power dry/wet
    od::Parameter mWeave{"Weave", 1.0f};  // 0..1, feedback-matrix architecture morph

  private:
    // Delay-line rings (256 KB) + diffuser + one-pole states. Class
    // members so any future NEON stays off the stack (no :64 hint trap;
    // feedback_neon_intrinsics_drumvoice).
    float mLine[kFdnLines][kFdnLineBufLen];
    float mDiff[4][kFdnDiffBufLen];
    int mDiffIdx[4];
    float mHfLp[kFdnLines];    // per-line HF-damping one-pole state
    float mBassLp[kFdnLines];  // per-line bass-shelf one-pole state
    float mBaseDelay[kFdnLines]; // per-line slewed base delay (samples)
    float mLfoX[kFdnLines];    // per-line magic-circle LFO state (x)
    float mLfoY[kFdnLines];    // per-line magic-circle LFO state (y)
    float mBody1[kFdnModes];   // wooden-body modal SVF state (ic1eq)
    float mBody2[kFdnModes];   // wooden-body modal SVF state (ic2eq)
    float mCrushPhase, mCrushHold; // wardrobe bitcrush decimator (mono)
    float mMuffle;             // wardrobe muffle one-pole LP state
    float mRmX, mRmY;          // wardrobe ring-mod carrier (magic circle)
    float mComb[kFdnCombBufLen]; // wardrobe comb delay (mono)
    int mCombW;
    bool mPrimed;              // base delays primed to target on first block
    int mWrite;
    float mDcX1, mDcY1;
#endif
  };

} // namespace stolmine
