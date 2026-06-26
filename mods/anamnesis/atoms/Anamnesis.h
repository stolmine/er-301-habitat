// anamnesis::Anamnesis
//
// Spatial-glitch instrument (CM4-only): a short-buffer micro-looper
// fused with a continuously-morphing spatial field via a global CLOCK
// (variable internal sample-rate), cross-fed and Spiral-governed.
// Internal-stereo (one object, shared coherent L/R field).
//
// Phase 1.1 (0.1.0.1): the spatial-field STAGE 2 — a unitary FDN tail.
//   - N=8 delay lines, coprime lengths, Householder feedback matrix
//     (A = I - (2/N) 11^T): unitary -> lossless prototype is stable.
//   - Per-line Jot T60 gain g_i = 10^(-3 m_i/(fs T60)): decay is
//     SIZE-INDEPENDENT (validated offline by fdn_t60_rig.cpp; avoids the
//     Network cascade-FDN postmortem failures #1 + #4).
//   - 4-stage Schroeder allpass input diffuser thickens early density.
//   - Stereo from decorrelated channel groups (0..3 = L, 4..7 = R).
// Size/Decay/Diffusion/Mix wired. The sparse multitap (Stage 1), the
// alpha-morph "plexus" axis, the looper, CLOCK, and cross-feedback
// arrive in later phases per planning/spatial-glitch-impl/99-build-order.md.
//
// DESIGN LAW (postmortem): the FDN feedback loop stays UNITARY; any
// future multitap "glitch" reflections are FEEDFORWARD only, never a
// weighted tap-sum inside this loop.
//
// Name: anamnesis (recollection / the captured past replayed and
// reshaped). No third-party branding per feedback_no_third_party_branding.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace anamnesis
{

  static const int kFdnN = 8;

  // Coprime (prime) base delays @48k -> ~34.8..92.6 ms at size 1.0.
  static const int kFdnBase[kFdnN] = {1669, 1987, 2311, 2833, 3299, 3671, 4049, 4447};

  // Size maps to a delay scale in [kSizeMin, kSizeMax]. Buffer must hold
  // the longest line at max size: 4447 * 2.0 = 8894 < 9000.
  static const float kSizeMin   = 0.25f;
  static const float kSizeMax   = 2.0f;
  static const int   kFdnBufLen = 9000;

  // Input diffuser: 4 series Schroeder allpasses, prime lengths.
  static const int kApN = 4;
  static const int kApLen[kApN] = {113, 211, 337, 449};
  static const int kApMax = 449;

  class Anamnesis : public od::Object
  {
  public:
    Anamnesis()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mSize);
      addParameter(mDecay);
      addParameter(mDiffusion);
      addParameter(mMix);

      memset(mLine, 0, sizeof(mLine));
      memset(mAp, 0, sizeof(mAp));
      memset(mApWr, 0, sizeof(mApWr));
      mWr = 0;
      mSizeScaleZ = 1.0f;
      mT60Z = 2.0f;
      mDiffGZ = 0.4f;
      mInit = false;
    }

    virtual ~Anamnesis() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};

    od::Parameter mSize{"Size", 0.5f};
    od::Parameter mDecay{"Decay", 0.5f};
    od::Parameter mDiffusion{"Diffusion", 0.6f};
    od::Parameter mMix{"Mix", 0.4f};

    // Flush-to-zero: denormal feedback tails stall the A72 FPU 10-100x.
    // FPCR is per-thread on aarch64 and NOT inherited; set on the audio
    // thread. NEON is always FTZ; this covers scalar paths.
    inline void ensureFlushToZero()
    {
#if defined(__aarch64__)
      if (!mFzSet)
      {
        uint64_t fpcr;
        __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
        fpcr |= (1ull << 24); // FZ bit
        __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
        mFzSet = true;
      }
#endif
    }

    // Fractional read of line i at delay d samples (linear interp).
    inline float readLine(int i, float d)
    {
      float rp = (float)mWr - d;
      while (rp < 0.0f) rp += (float)kFdnBufLen;
      int i0 = (int)rp;
      float fr = rp - (float)i0;
      int i1 = i0 + 1; if (i1 >= kFdnBufLen) i1 -= kFdnBufLen;
      return mLine[i][i0] + (mLine[i][i1] - mLine[i][i0]) * fr;
    }

    virtual void process()
    {
      ensureFlushToZero();

      const float *inL = mInL.buffer();
      const float *inR = mInR.buffer();
      float *outL = mOutL.buffer();
      float *outR = mOutR.buffer();

      const float fs = (float)globalConfig.sampleRate;

      // ---- block-rate control read + smoothing ----
      float sizeN = clampf(mSize.value(), 0.0f, 1.0f);
      float decayN = clampf(mDecay.value(), 0.0f, 1.0f);
      float diffN = clampf(mDiffusion.value(), 0.0f, 1.0f);
      float mix = clampf(mMix.value(), 0.0f, 1.0f);

      const float sizeScaleTgt = kSizeMin + sizeN * (kSizeMax - kSizeMin);
      const float t60Tgt = 0.2f * powf(100.0f, decayN); // 0.2 .. 20 s
      const float diffGTgt = diffN * 0.75f;             // allpass coef

      if (!mInit) { mSizeScaleZ = sizeScaleTgt; mT60Z = t60Tgt; mDiffGZ = diffGTgt; mInit = true; }
      // T60 and Diffusion smooth per block (they do not zipper). SIZE is
      // smoothed PER SAMPLE inside the loop instead: a per-block step in
      // delay length is the zipper source. Gliding the read pointer per
      // sample is the Pecto-style REPITCH fix -- the tail Doppler-glides
      // during a Size sweep (tape / Erbe-Verb character), on-brand with
      // the Mood "sample-rate = pitch+length" identity. (Network's FADE
      // crossfade avoids repitch but is awkward in a recirculating FDN;
      // we want the glide here.)
      const float aBlk = 1.0f - expf(-(float)FRAMELENGTH / (fs * 0.030f)); // ~30 ms
      mT60Z   += aBlk * (t60Tgt - mT60Z);
      mDiffGZ += aBlk * (diffGTgt - mDiffGZ);
      const float aSize = 1.0f - expf(-1.0f / (fs * 0.040f)); // per-sample, ~40 ms

      // Per-line Jot decay gain from the current (block-start) size. The
      // gain moves slowly, so a per-block recompute is fine even though
      // the read length glides per sample below.
      float g[kFdnN];
      for (int i = 0; i < kFdnN; i++)
      {
        float L = (float)kFdnBase[i] * mSizeScaleZ;
        if (L < 1.0f) L = 1.0f;
        if (L > (float)(kFdnBufLen - 2)) L = (float)(kFdnBufLen - 2);
        float gi = powf(10.0f, -3.0f * L / (fs * mT60Z));
        if (gi > 0.9999f) gi = 0.9999f;   // never reach/exceed lossless
        g[i] = gi;
      }
      const float apG = mDiffGZ;
      const float fMix = 2.0f / (float)kFdnN; // Householder scale
      const float wetGain = 0.5f;

      for (int n = 0; n < FRAMELENGTH; n++)
      {
        // per-sample Size glide (REPITCH; removes the per-block zipper)
        mSizeScaleZ += aSize * (sizeScaleTgt - mSizeScaleZ);

        const float dryL = inL[n];
        const float dryR = inR[n];

        // mono drive into the field
        float x = (dryL + dryR) * 0.5f;

        // ---- 4-stage Schroeder allpass input diffuser ----
        for (int k = 0; k < kApN; k++)
        {
          int idx = mApWr[k];
          float zD = mAp[k][idx];
          float v = x + apG * zD;
          float y = -apG * v + zD;
          mAp[k][idx] = v;
          idx++; if (idx >= kApLen[k]) idx = 0;
          mApWr[k] = idx;
          x = y;
        }

        // ---- read 8 lines (per-sample glided delay), Householder, inject ----
        float r[kFdnN], s = 0.0f;
        for (int i = 0; i < kFdnN; i++)
        {
          float L = (float)kFdnBase[i] * mSizeScaleZ;
          if (L < 1.0f) L = 1.0f;
          if (L > (float)(kFdnBufLen - 2)) L = (float)(kFdnBufLen - 2);
          r[i] = readLine(i, L);
          s += r[i];
        }

        for (int i = 0; i < kFdnN; i++)
        {
          float mixed = r[i] - fMix * s;     // unitary Householder reflect
          float w = x + g[i] * mixed;        // input + decayed feedback
          if (!isfinitef(w)) w = 0.0f;       // NaN/Inf guard
          if (w > 8.0f) w = 8.0f; else if (w < -8.0f) w = -8.0f; // safety wall
          mLine[i][mWr] = w;
        }

        // ---- decorrelated stereo taps (Householder already mixes) ----
        float wetL = (r[0] + r[1] + r[2] + r[3]) * wetGain;
        float wetR = (r[4] + r[5] + r[6] + r[7]) * wetGain;

        mWr++; if (mWr >= kFdnBufLen) mWr = 0;

        outL[n] = dryL + mix * (wetL - dryL);
        outR[n] = dryR + mix * (wetR - dryR);
      }
    }

    static inline float clampf(float v, float lo, float hi)
    {
      return v < lo ? lo : (v > hi ? hi : v);
    }
    static inline bool isfinitef(float v)
    {
      // NaN -> (v==v) false; +/-Inf -> magnitude check false.
      return (v == v) && (v <= 3.0e38f) && (v >= -3.0e38f);
    }

    // state
    float mLine[kFdnN][kFdnBufLen];
    float mAp[kApN][kApMax];
    int   mApWr[kApN];
    int   mWr;
    float mSizeScaleZ, mT60Z, mDiffGZ;
    bool  mInit = false;
    bool  mFzSet = false;
#endif
  };

} // namespace anamnesis
