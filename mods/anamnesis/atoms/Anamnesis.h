// anamnesis::Anamnesis
//
// Spatial-glitch instrument (CM4-only): a short-buffer micro-looper
// fused with a continuously-morphing spatial field via a global CLOCK,
// cross-fed and Spiral-governed. Internal-stereo (one object, shared
// coherent L/R field).
//
// Phase 1.3 (0.1.0.4): the spatial field, BOTH stages of the Moorer
// separation that resolves the Network cascade-FDN postmortem, plus the
// Erbe-Verb alpha-matrix morph -- the complete "plexus" axis.
//   STAGE 1 -- sparse FEEDFORWARD early-reflection taps off their own
//     buffer (NEVER in a feedback loop): N addressable taps, ping-pong
//     pan, light decorrelating jitter. The "glitchy/addressable" pole.
//   STAGE 2 -- unitary N=8 FDN tail, per-line Jot T60 decay (size-
//     independent; offline-validated) + 4-stage Schroeder input diffuser.
//   DENSITY (the plexus macro) drives BOTH: (a) crossfade Stage-1 taps ->
//     Stage-2 FDN, AND (b) the FDN feedback matrix A(a) = I - a*(2/N)11^T
//     from identity (a=0: 8 independent combs, sparse looping modes) to
//     full Householder (a=1: dense diffuse wash). So the WHOLE field
//     travels sparse->dense, not just the tap/wash balance.
// Size glides per-sample (REPITCH) so sweeps Doppler-glide, no zipper.
//
// DESIGN LAW (postmortem): the FDN loop stays UNITARY; the sparse taps
// are FEEDFORWARD only, never a weighted tap-sum inside the loop.
//
// Looper, CLOCK, and cross-feedback arrive in later phases per
// planning/spatial-glitch-impl/99-build-order.md. No third-party
// branding per feedback_no_third_party_branding.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace anamnesis
{

  static const float kPi = 3.14159265358979f;

  // ---- Stage 2 FDN ----
  static const int kFdnN = 8;
  static const int kFdnBase[kFdnN] = {1669, 1987, 2311, 2833, 3299, 3671, 4049, 4447};
  static const float kSizeMin   = 0.25f;
  static const float kSizeMax   = 2.0f;
  static const int   kFdnBufLen = 9000;   // > 4447 * kSizeMax

  // input diffuser: 4 series Schroeder allpasses
  static const int kApN = 4;
  static const int kApLen[kApN] = {113, 211, 337, 449};
  static const int kApMax = 449;

  // ---- Stage 1 sparse taps ----
  static const int kTapN = 12;
  // base tap delays (samples @ size 1.0), ~20..400 ms, irregular spacing
  static const int kTapBase[kTapN] =
    {960, 1597, 2311, 3001, 4099, 5273, 6571, 8089, 9743, 12101, 15307, 19211};
  static const int kTapBufLen = 39000;    // > 19211 * kSizeMax

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
      addParameter(mDensity);
      addParameter(mMix);

      memset(mLine, 0, sizeof(mLine));
      memset(mAp, 0, sizeof(mAp));
      memset(mApWr, 0, sizeof(mApWr));
      memset(mTapBuf, 0, sizeof(mTapBuf));
      mWr = 0;
      mTapWr = 0;
      mSizeScaleZ = 1.0f;
      mT60Z = 2.0f;
      mDiffGZ = 0.4f;
      mDensityZ = 0.5f;
      mInit = false;

      // Per-tap pan (equal-power ping-pong), gain (decay with delay),
      // and a slow decorrelated jitter LFO (breaks comb among taps).
      for (int i = 0; i < kTapN; i++)
      {
        float side = (i & 1) ? 1.0f : -1.0f;
        float spread = 0.30f + 0.70f * (float)i / (float)(kTapN - 1);
        float pan = side * spread;                    // -1..+1
        float ang = (pan * 0.5f + 0.5f) * (kPi * 0.5f); // 0..pi/2
        mPanL[i] = cosf(ang);
        mPanR[i] = sinf(ang);
        mTapGain[i] = expf(-1.8f * (float)i / (float)(kTapN - 1)); // 1..~0.16
        mTapLfoPhase[i] = (float)i * 0.37f;
        mTapLfoHz[i] = 0.30f + 0.08f * (float)i;       // ~0.3..1.2 Hz
      }
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
    od::Parameter mDensity{"Density", 0.5f};
    od::Parameter mMix{"Mix", 0.4f};

    inline void ensureFlushToZero()
    {
#if defined(__aarch64__)
      if (!mFzSet)
      {
        uint64_t fpcr;
        __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
        fpcr |= (1ull << 24); // FZ
        __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
        mFzSet = true;
      }
#endif
    }

    inline float readLine(int i, float d)
    {
      float rp = (float)mWr - d;
      while (rp < 0.0f) rp += (float)kFdnBufLen;
      int i0 = (int)rp; float fr = rp - (float)i0;
      int i1 = i0 + 1; if (i1 >= kFdnBufLen) i1 -= kFdnBufLen;
      return mLine[i][i0] + (mLine[i][i1] - mLine[i][i0]) * fr;
    }

    inline float readTap(float d)
    {
      float rp = (float)mTapWr - d;
      while (rp < 0.0f) rp += (float)kTapBufLen;
      int i0 = (int)rp; float fr = rp - (float)i0;
      int i1 = i0 + 1; if (i1 >= kTapBufLen) i1 -= kTapBufLen;
      return mTapBuf[i0] + (mTapBuf[i1] - mTapBuf[i0]) * fr;
    }

    virtual void process()
    {
      ensureFlushToZero();

      const float *inL = mInL.buffer();
      const float *inR = mInR.buffer();
      float *outL = mOutL.buffer();
      float *outR = mOutR.buffer();

      const float fs = (float)globalConfig.sampleRate;

      float sizeN = clampf(mSize.value(), 0.0f, 1.0f);
      float decayN = clampf(mDecay.value(), 0.0f, 1.0f);
      float diffN = clampf(mDiffusion.value(), 0.0f, 1.0f);
      float density = clampf(mDensity.value(), 0.0f, 1.0f);
      float mix = clampf(mMix.value(), 0.0f, 1.0f);

      const float sizeScaleTgt = kSizeMin + sizeN * (kSizeMax - kSizeMin);
      const float t60Tgt = 0.2f * powf(100.0f, decayN); // 0.2..20 s
      const float diffGTgt = diffN * 0.75f;

      if (!mInit) { mSizeScaleZ = sizeScaleTgt; mT60Z = t60Tgt; mDiffGZ = diffGTgt; mDensityZ = density; mInit = true; }
      const float aBlk = 1.0f - expf(-(float)FRAMELENGTH / (fs * 0.030f));
      mT60Z     += aBlk * (t60Tgt - mT60Z);
      mDiffGZ   += aBlk * (diffGTgt - mDiffGZ);
      mDensityZ += aBlk * (density - mDensityZ);
      const float aSize = 1.0f - expf(-1.0f / (fs * 0.040f)); // per-sample REPITCH glide

      // Stage 2 per-line Jot decay gain (block-rate; slow-moving).
      float g[kFdnN];
      for (int i = 0; i < kFdnN; i++)
      {
        float L = (float)kFdnBase[i] * mSizeScaleZ;
        if (L < 1.0f) L = 1.0f;
        if (L > (float)(kFdnBufLen - 2)) L = (float)(kFdnBufLen - 2);
        float gi = powf(10.0f, -3.0f * L / (fs * mT60Z));
        if (gi > 0.9999f) gi = 0.9999f;
        g[i] = gi;
      }
      const float apG = mDiffGZ;
      // Erbe-Verb alpha-morph: A(a) = I - a*(2/N)*11^T, a = Density.
      // a=0 -> identity (8 independent combs, sparse looping modes);
      // a=1 -> full Householder (dense diffuse wash). Contraction at all
      // a (stable), lossless on the 7 differential reverb modes.
      const float fMixA = mDensityZ * 2.0f / (float)kFdnN;
      const float kFdnWetGain = 0.5f;
      const float kTapWetGain = 0.35f;

      // tap jitter LFO increments (per sample)
      float lfoInc[kTapN];
      for (int i = 0; i < kTapN; i++) lfoInc[i] = 2.0f * kPi * mTapLfoHz[i] / fs;
      const float kJitDepth = 2.5f; // samples

      for (int n = 0; n < FRAMELENGTH; n++)
      {
        // per-sample Size glide (REPITCH; removes per-block zipper)
        mSizeScaleZ += aSize * (sizeScaleTgt - mSizeScaleZ);

        const float dryL = inL[n];
        const float dryR = inR[n];
        const float xRaw = (dryL + dryR) * 0.5f;

        // ================= STAGE 1: sparse feedforward taps =================
        mTapBuf[mTapWr] = xRaw;                   // write raw input
        float tapL = 0.0f, tapR = 0.0f;
        for (int i = 0; i < kTapN; i++)
        {
          mTapLfoPhase[i] += lfoInc[i];
          if (mTapLfoPhase[i] > 2.0f * kPi) mTapLfoPhase[i] -= 2.0f * kPi;
          float d = (float)kTapBase[i] * mSizeScaleZ + sinf(mTapLfoPhase[i]) * kJitDepth;
          if (d < 1.0f) d = 1.0f;
          if (d > (float)(kTapBufLen - 2)) d = (float)(kTapBufLen - 2);
          float t = readTap(d) * mTapGain[i];
          tapL += t * mPanL[i];
          tapR += t * mPanR[i];
        }
        tapL *= kTapWetGain;
        tapR *= kTapWetGain;
        mTapWr++; if (mTapWr >= kTapBufLen) mTapWr = 0;

        // ================= STAGE 2: unitary FDN tail =================
        float x = xRaw;
        for (int k = 0; k < kApN; k++)            // 4-stage Schroeder diffuser
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
          float mixed = r[i] - fMixA * s;         // alpha-morph: I(0) -> Householder(1)
          float w = x + g[i] * mixed;
          if (!isfinitef(w)) w = 0.0f;
          if (w > 8.0f) w = 8.0f; else if (w < -8.0f) w = -8.0f;
          mLine[i][mWr] = w;
        }
        float fdnL = (r[0] + r[1] + r[2] + r[3]) * kFdnWetGain;
        float fdnR = (r[4] + r[5] + r[6] + r[7]) * kFdnWetGain;
        mWr++; if (mWr >= kFdnBufLen) mWr = 0;

        // ============ DENSITY crossfade: sparse taps <-> dense FDN ============
        float wetL = tapL + mDensityZ * (fdnL - tapL);
        float wetR = tapR + mDensityZ * (fdnR - tapR);

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
      return (v == v) && (v <= 3.0e38f) && (v >= -3.0e38f);
    }

    // ---- state ----
    float mLine[kFdnN][kFdnBufLen];
    float mAp[kApN][kApMax];
    int   mApWr[kApN];
    int   mWr;
    float mTapBuf[kTapBufLen];
    int   mTapWr;
    float mPanL[kTapN], mPanR[kTapN], mTapGain[kTapN];
    float mTapLfoPhase[kTapN], mTapLfoHz[kTapN];
    float mSizeScaleZ, mT60Z, mDiffGZ, mDensityZ;
    bool  mInit = false;
    bool  mFzSet = false;
#endif
  };

} // namespace anamnesis
