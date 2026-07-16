#pragma once

// FDNTank -- a feedback-delay-network reverb core (working name; see
// planning/fdn-reverb-design.md). Phase 1 = a dense, smooth, audible
// scaffold: 8 delay lines, a lossless Householder feedback matrix,
// per-line one-pole HF damping, a 4-stage Schroeder input diffuser, and
// an equal-power dry/wet crossfade. Internal-stereo (one shared tank,
// decorrelated L/R output taps) so the Lua wiring just maps In1/In2 ->
// In L/In R and Out L/Out R -> Out1/Out2 (the Fabula pattern).
//
// The point of this topology (vs Fabula's Dattorro tank) is the NEON
// ceiling: fixed / block-modulated delay lengths mean CONTIGUOUS reads
// at a moving write head (not per-sample gathers), and the matrix is a
// reduce + broadcast-subtract. Phase 1 keeps the DSP SCALAR and
// am335x-safe so we can hear it and A/B the tail; the NEON pass
// (matrix + damping bank + output dot-products) is Phase 2, once the
// voicing is settled. See feedback_neon_soa_svf_bank / Visadhara.h for
// the vectorization template.
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
      addParameter(mDamp);
      addParameter(mMix);

      memset(mLine, 0, sizeof(mLine));
      memset(mDiff, 0, sizeof(mDiff));
      memset(mDampState, 0, sizeof(mDampState));
      for (int a = 0; a < 4; a++) mDiffIdx[a] = 0;
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

      float dampN = mDamp.value();
      if (!(dampN >= 0.0f)) dampN = 0.0f;
      if (dampN > 1.0f) dampN = 1.0f;

      float mixN = mMix.value();
      if (!(mixN >= 0.0f)) mixN = 0.0f;
      if (mixN > 1.0f) mixN = 1.0f;

      // Size -> integer line lengths (block rate). 0.1..1.0 of the base
      // delays; clamped into the ring. Reads stay contiguous per sample.
      const float sizeScale = 0.1f + 0.9f * sizeN;
      int L[kFdnLines];
      for (int i = 0; i < kFdnLines; i++)
      {
        int l = (int)(kFdnBaseMs[i] * 0.001f * globalConfig.sampleRate * sizeScale);
        if (l < 4) l = 4;
        if (l > kFdnLineMask) l = kFdnLineMask;
        L[i] = l;
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

      // Decay -> loop gain. Lossless Householder + g<1 guarantees a
      // decaying tail; 0.97 ceiling keeps a long-but-finite RT60. (The
      // perceptual RT60 curve is a Phase-2 voicing choice.)
      const float g = decayN * 0.97f;

      // Damp -> one-pole LP coefficient. dampN 0 = bright (a=1, no
      // damping), dampN 1 = dark (a=0.1). Passive, so it only removes
      // loop energy -> stays stable.
      const float dampA = 1.0f - 0.9f * dampN;

      // Equal-power (sqrt-law) dry/wet: the wet is decorrelated from the
      // dry, so a linear crossfade would dip ~3 dB at center. This is the
      // house standard (feedback_equal_power_drywet_crossfade).
      const float dryG = sqrtf(1.0f - mixN);
      const float wetG = sqrtf(mixN);

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

        // Read the 8 delayed line outputs (contiguous per line).
        float d[kFdnLines];
        for (int i = 0; i < kFdnLines; i++)
        {
          const int ri = (mWrite - L[i]) & kFdnLineMask;
          d[i] = mLine[i][ri];
        }

        // Per-line one-pole HF damping (SoA states; the future NEON bank).
        for (int i = 0; i < kFdnLines; i++)
        {
          mDampState[i] += dampA * (d[i] - mDampState[i]);
          d[i] = mDampState[i];
        }

        // Householder reflection: f = d - (2/N) * sum(d) = d - 0.25*sum.
        // Orthonormal (lossless) -> stability comes from the g<1 scale.
        float s = 0.0f;
        for (int i = 0; i < kFdnLines; i++) s += d[i];
        s *= 0.25f;

        // Write feedback + injection; accumulate the stereo wet taps.
        float wetL = 0.0f;
        float wetR = 0.0f;
        for (int i = 0; i < kFdnLines; i++)
        {
          float fb = g * (d[i] - s);
          // Pure blow-up guard (the math already guarantees decay); this
          // never engages in normal use, so it colors nothing. A voiced
          // soft-saturator is a Phase-2 decision.
          if (fb > 16.0f) fb = 16.0f;
          else if (fb < -16.0f) fb = -16.0f;

          mLine[i][mWrite] = inject * kFdnInj[i] + fb;

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
    od::Parameter mDecay{"Decay", 0.6f};  // 0..1, loop gain (RT60)
    od::Parameter mDamp{"Damp", 0.3f};    // 0..1, HF damping (dark tail)
    od::Parameter mMix{"Mix", 0.35f};     // 0..1, equal-power dry/wet

  private:
    // Delay-line rings (256 KB) + diffuser + one-pole states. Class
    // members so any future NEON stays off the stack (no :64 hint trap;
    // feedback_neon_intrinsics_drumvoice).
    float mLine[kFdnLines][kFdnLineBufLen];
    float mDiff[4][kFdnDiffBufLen];
    int mDiffIdx[4];
    float mDampState[kFdnLines];
    int mWrite;
    float mDcX1, mDcY1;
#endif
  };

} // namespace stolmine
