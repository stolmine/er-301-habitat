#pragma once

// FDNTank -- a feedback-delay-network reverb core (working name; see
// planning/fdn-reverb-design.md). 8 delay lines, a lossless Householder
// feedback matrix, per-line frequency-dependent loss filters (Phase 2:
// RT60-based gain + HF damping + bass low-shelf = the "spectral RT60"),
// modulated fractional delay reads (Phase 2d: per-line slow LFOs
// de-metalize the tail, base-delay slew gives Doppler-smooth Size
// sweeps), a 4-stage Schroeder input diffuser, an equal-power dry/wet
// crossfade, and (Phase 3.1) a coupled 8-band SVF filterbank send that
// refeeds a spectrally-shaped tap of the tank field back into the tank
// -- with decorrelated per-band focus drift and input cross-synthesis
// (the dry input's per-band envelope keys the refeed, so the reverb
// tracks your playing; keying releases toward freeze as the macro rises).
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

  // ---- Phase-3 spectral layer: coupled filterbank vocoder --------------------
  // An 8-band SVF bandpass bank analyzes a mono tap of the tank field; the
  // (P3.1: contour/tilt) shaped sum is soft-limited and REFED into the tank
  // input, so the FDN re-densifies the spectrally-shaped energy. The Spectral
  // macro is both a refeed-amount ramp (0 = send off) and the station morph
  // (P3.1 has only the contour station; gate/bloom/freeze land in P3.2-P3.3).
  static const int kFdnBands = 8;
  // Log-spaced band centers ~100 Hz .. 8 kHz (Hz). Fixed in P3.1; a "spectral
  // focus" sub will roll these around in P3.4.
  static const float kFdnBandHz[kFdnBands] = {
    100.0f, 187.0f, 350.0f, 654.0f, 1223.0f, 2287.0f, 4277.0f, 8000.0f
  };
  static const float kFdnBandDamp  = 0.5f;    // SVF 1/Q (Q = 2)
  static const float kFdnRefeedCeil = 0.25f;  // max refeed level at Spectral=1
  static const float kFdnSpectralOn = 0.001f; // below this the bank is bypassed

  // Bright tilt on the band sum, attached to ABSOLUTE frequency (sqrt(fc/900),
  // +3 dB/oct-ish) so the refed energy is pulled UP off the low-mid mud that a
  // flat static refeed locks onto. Precomputed at the base centers; the block-
  // rate focus motion multiplies in the sweep's contribution (tiltGlobal).
  static const float kFdnBandTiltBase[kFdnBands] = {
    0.333f, 0.456f, 0.624f, 0.852f, 1.166f, 1.594f, 2.180f, 2.981f
  };
  // "Spectral focus" motion: a slow magic-circle LFO PER BAND, each at a
  // decorrelated rate, so the band centers drift INDEPENDENTLY (the spectrum
  // churns) rather than sweeping coherently (which read as a phaser). Each
  // wanders +/- depth octaves.
  static const float kFdnFocusRateHz[kFdnBands] = {
    0.071f, 0.089f, 0.103f, 0.127f, 0.061f, 0.113f, 0.083f, 0.097f
  };
  static const float kFdnFocusDepthOct = 0.8f;  // +/- 0.8 octave per band
  static const float kFdnPrewarpMax    = 0.55f; // cap tan-approx arg (validity)

  // Input cross-synthesis (vocoder keying): a second 8-band analysis of the DRY
  // input (same band centers) follows each band's envelope; the tank carrier is
  // gated per band by that envelope so the reverb spectrally TRACKS the input.
  // The keying amount is strong at low knob and RELEASES toward freeze at the
  // top (reactive -> autonomous), via keyAmt = clamp((1-Spectral)*2).
  static const float kFdnEnvTau    = 0.030f;  // input band envelope follower (s)
  static const float kFdnKeyThresh = 0.02f;   // per-band gate threshold (env)
  static const float kFdnKeySlope  = 8.0f;    // gate ramp above threshold

  // Cheap tan approximation for the TPT-SVF prewarp g = tan(pi*fc/SR), valid
  // over the band range (arg < ~0.6 rad): x + x^3/3 + 2x^5/15. Avoids runtime
  // tanf/sinf (feedback_package_trig_lut) and is block-rate anyway.
  static inline float fdnTanApprox(float x)
  {
    const float x2 = x * x;
    return x * (1.0f + x2 * (0.33333333f + x2 * 0.13333333f));
  }

  // Bounded soft governor for the refeed (x / (1 + |x|)): near-identity for
  // small x, hard-bounded to +/-1 -> the coupling can never run away
  // ([[feedback_spiral_feedback_governor]]). All hardware VFP (vabs + vdiv),
  // no per-sample libcall (sqrtf compiles to bl <sqrtf> here, not vsqrt).
  static inline float fdnSoftGov(float x)
  {
    return x / (1.0f + fabsf(x));
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
      addParameter(mSpectral);

      memset(mLine, 0, sizeof(mLine));
      memset(mDiff, 0, sizeof(mDiff));
      memset(mHfLp, 0, sizeof(mHfLp));
      memset(mBassLp, 0, sizeof(mBassLp));
      memset(mSvf1, 0, sizeof(mSvf1));
      memset(mSvf2, 0, sizeof(mSvf2));
      memset(mInSvf1, 0, sizeof(mInSvf1));
      memset(mInSvf2, 0, sizeof(mInSvf2));
      memset(mInEnv, 0, sizeof(mInEnv));
      for (int b = 0; b < kFdnBands; b++)
      {
        mFocusX[b] = kFdnLfoX0[b];   // decorrelated focus-LFO start phases
        mFocusY[b] = kFdnLfoY0[b];
      }
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

      // ---- Phase-3 spectral send (block rate) ----
      float spectralN = mSpectral.value();
      if (!(spectralN >= 0.0f)) spectralN = 0.0f;
      if (spectralN > 1.0f) spectralN = 1.0f;
      const bool spectralActive = spectralN > kFdnSpectralOn;
      const float refeedLevel = spectralN * kFdnRefeedCeil;
      // TPT-SVF bandpass coefficients per band. g = tan(pi*fc/SR) (tan approx);
      // a1 = 1/(1 + g*(g + 1/Q)); a2 = g*a1; a3 = g*a2. Band gains are flat in
      // P3.1 -- the contour/tilt curve and the movable focus arrive in P3.4.
      float svfA1[kFdnBands], svfA2[kFdnBands], svfA3[kFdnBands];
      float bandGain[kFdnBands];
      if (spectralActive)
      {
        // Per-band spectral-focus drift (decorrelated magic-circle LFOs, block
        // rate): each band center wanders independently by focusMult =
        // 2^(depth*lfo_b) octaves so the spectrum churns rather than sweeping
        // as one (coherent motion read as a phaser). Bright tilt is on absolute
        // frequency (sqrt(focusMult_b)) so higher-swept bands brighten -> energy
        // stays off the low-mid mud.
        for (int b = 0; b < kFdnBands; b++)
        {
          const float epsF =
            6.2831853f * kFdnFocusRateHz[b] * (float)FRAMELENGTH * invSR;
          mFocusX[b] += epsF * mFocusY[b];
          mFocusY[b] -= epsF * mFocusX[b];
          const float focusMult = powf(2.0f, kFdnFocusDepthOct * mFocusX[b]);
          float arg = 3.14159265f * kFdnBandHz[b] * focusMult * invSR;
          if (arg > kFdnPrewarpMax) arg = kFdnPrewarpMax;   // keep tan-approx valid
          const float g = fdnTanApprox(arg);
          const float a1 = 1.0f / (1.0f + g * (g + kFdnBandDamp));
          svfA1[b] = a1;
          svfA2[b] = g * a1;
          svfA3[b] = g * svfA2[b];
          bandGain[b] = kFdnBandTiltBase[b] * sqrtf(focusMult);
        }
      }
      // Input-envelope follower coefficient + keying amount. keyAmt holds full
      // through the reactive lower half then releases toward freeze at the top.
      const float envCoef = 1.0f - expf(-1.0f / (kFdnEnvTau * globalConfig.sampleRate));
      float keyAmt = (1.0f - spectralN) * 2.0f;
      if (keyAmt < 0.0f) keyAmt = 0.0f;
      else if (keyAmt > 1.0f) keyAmt = 1.0f;
      const float baseKey = 1.0f - keyAmt;   // full-pass floor (autonomous end)

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

        // ---- Phase-3 spectral send: analyze the tank field, shape, refeed ----
        // Mono field tap -> 8-band TPT-SVF bandpass -> (P3.1) flat sum ->
        // bounded governor -> refeed into the tank input. The FDN re-densifies
        // the shaped energy; the governor keeps the coupling from running away.
        float refeedInject = 0.0f;
        if (spectralActive)
        {
          // Modulator: analyze the dry input (hp) into the same 8 bands and
          // follow each band's envelope (the vocoder's control signal).
          for (int b = 0; b < kFdnBands; b++)
          {
            const float u3 = hp - mInSvf2[b];
            const float u1 = svfA1[b] * mInSvf1[b] + svfA2[b] * u3;
            const float u2 = mInSvf2[b] + svfA2[b] * mInSvf1[b] + svfA3[b] * u3;
            mInSvf1[b] = 2.0f * u1 - mInSvf1[b];
            mInSvf2[b] = 2.0f * u2 - mInSvf2[b];
            const float mag = u1 < 0.0f ? -u1 : u1;
            mInEnv[b] += envCoef * (mag - mInEnv[b]);
          }

          // Carrier: the tank field, keyed per band by the input envelope.
          // effKey blends the full-pass floor (autonomous/freeze end) with the
          // input gate (reactive end); keyAmt releases as the macro rises.
          float tankTap = 0.0f;
          for (int i = 0; i < kFdnLines; i++) tankTap += d[i];
          tankTap *= 0.125f;   // 1/8 mono average

          float spectralOut = 0.0f;
          for (int b = 0; b < kFdnBands; b++)
          {
            const float v3 = tankTap - mSvf2[b];
            const float v1 = svfA1[b] * mSvf1[b] + svfA2[b] * v3;   // bandpass
            const float v2 = mSvf2[b] + svfA2[b] * mSvf1[b] + svfA3[b] * v3;
            mSvf1[b] = 2.0f * v1 - mSvf1[b];
            mSvf2[b] = 2.0f * v2 - mSvf2[b];

            // Soft per-band gate from the input envelope, blended by keyAmt.
            float keyGain = (mInEnv[b] - kFdnKeyThresh) * kFdnKeySlope;
            if (keyGain < 0.0f) keyGain = 0.0f;
            else if (keyGain > 1.0f) keyGain = 1.0f;
            const float effKey = baseKey + keyAmt * keyGain;

            spectralOut += bandGain[b] * v1 * effKey;
          }
          refeedInject = refeedLevel * fdnSoftGov(spectralOut);
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

        // Lossless Householder reflection on the loss-filtered signal:
        // out = loss - (2/N)*sum(loss). All decay lives in loss[]; the matrix
        // preserves energy, so stability follows from the RT60 gain clamps.
        float s = 0.0f;
        for (int i = 0; i < kFdnLines; i++) s += loss[i];
        s *= 0.25f;

        // Write feedback + injection; accumulate the stereo wet taps.
        float wetL = 0.0f;
        float wetR = 0.0f;
        for (int i = 0; i < kFdnLines; i++)
        {
          float fb = loss[i] - s;
          // Pure blow-up guard (RT60 gains are clamped < 1, so this never
          // engages in normal use and colors nothing). A voiced in-loop
          // soft-saturator is a later Phase-2 item.
          if (fb > 16.0f) fb = 16.0f;
          else if (fb < -16.0f) fb = -16.0f;

          mLine[i][mWrite] = (inject + refeedInject) * kFdnInj[i] + fb;

          wetL += d[i] * kFdnOutL[i];
          wetR += d[i] * kFdnOutR[i];
        }
        mWrite = (mWrite + 1) & kFdnLineMask;

        wetL *= kFdnOutNorm * kFdnWetMakeup;
        wetR *= kFdnOutNorm * kFdnWetMakeup;

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
    od::Parameter mSpectral{"Spectral", 0.0f}; // 0..1, coupled filterbank macro

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
    float mSvf1[kFdnBands];    // carrier (tank) SVF state (ic1eq) per band
    float mSvf2[kFdnBands];    // carrier (tank) SVF state (ic2eq) per band
    float mInSvf1[kFdnBands];  // modulator (input) SVF state (ic1eq) per band
    float mInSvf2[kFdnBands];  // modulator (input) SVF state (ic2eq) per band
    float mInEnv[kFdnBands];   // per-band input envelope (vocoder keying)
    float mFocusX[kFdnBands];  // per-band magic-circle focus LFO state (x)
    float mFocusY[kFdnBands];  // per-band magic-circle focus LFO state (y)
    bool mPrimed;              // base delays primed to target on first block
    int mWrite;
    float mDcX1, mDcY1;
#endif
  };

} // namespace stolmine
