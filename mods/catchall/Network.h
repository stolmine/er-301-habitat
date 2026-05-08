#pragma once

// Network — non-traditional reverb / "macro spatial simulation".
// Phase 1: 32-tap stereo with 2D virtual reflector geometry, listener
// motion on a circular orbit, per-tap (delay, gainL, gainR) derived
// from distance + azimuth. Per-sample lerp on per-tap delays gives
// coherent Doppler-coupled slew across the whole field as the
// listener moves.
//
// Phases:
//  - Phase 0 (✓): 32-tap mono baseline lifting Pecto's NEON multi-tap
//                 infrastructure verbatim. Random fixed tap positions.
//  - Phase 1 (this): geometry generator + per-tap pan/gain + stereo
//                    + density + motion + seed.
//  - Phase 2: sparse selectable feedback recycling (per-tap fb_weight).
//  - Phase 3: FxEngine soften diffusion stage + 2D field viz.
//  - Phase 4: test procedures, version bump, release.
//
// All virtuals defined inline in this header per
// feedback_no_out_of_line_virtuals (vtable must be COMDAT-linked,
// immune to firmware/package vtable drift). No Network.cpp.
//
// See planning/network-implementation-plan.md for full plan.

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <new>

#include "network/trig_lut.h"
#include "network/geometry.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define NETWORK_HAS_NEON 1
#endif

namespace stolmine
{

  static const int kMaxNetworkTaps = 64;

  // Block-rate copy helper for class-member float arrays. noinline +
  // no-tree-vectorize prevents gcc from auto-vectorizing into quad-D
  // vst1.64 :64 stores, which trap on Cortex-A8 when the destination
  // class-member offset isn't 8-byte aligned (per
  // feedback_neon_intrinsics_drumvoice / Pecto.cpp:24-28 pattern).
  // Wrapped in SWIGLUA guard — SWIG can't parse the GCC attribute.
#ifndef SWIGLUA
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void networkCopyFloatArray(float *dst, const float *src, int n)
  {
    for (int i = 0; i < n; i++) dst[i] = src[i];
  }
#endif

  class Network : public od::Object
  {
  public:
    Network()
    {
      addInput(mIn);
      addOutput(mOut);
      addOutput(mOutR);
      addParameter(mSize);
      addParameter(mDensity);
      addParameter(mMotion);
      addParameter(mDecay);
      addParameter(mWet);
      addParameter(mInputLevel);
      addParameter(mSeed);

      // Initial reflector field at default seed.
      mLastSeed = 0xC0FFEE17u;
      network_geom::regenerateField(mReflectors, kMaxNetworkTaps, mLastSeed);

      // Zero per-tap state.
      for (int i = 0; i < kMaxNetworkTaps; i++)
      {
        mTapDelayTarget[i] = 0.0f;
        mTapGainL[i] = 0.0f;
        mTapGainR[i] = 0.0f;
        mTapDelaySmoothed[i] = 0.0f;
        mTapGainLSmoothed[i] = 0.0f;
        mTapGainRSmoothed[i] = 0.0f;
      }

      mWriteIndex = 0;
      mBuffer = 0;
      mMaxDelayInSamples = 0;
      mFirstProcess = true;
    }

    virtual ~Network()
    {
      deallocate();
    }

    // SWIG-visible.
    float allocateTimeUpTo(float seconds)
    {
      const int Ns_target = (int)(globalConfig.sampleRate * MAX(0.001f, seconds));
      const int Nf = (Ns_target / FRAMELENGTH + 1);
      const int Ns = Nf * FRAMELENGTH;
      if (allocate(Ns))
      {
        mMaxDelayInSamples = Ns;
        return (float)Ns / globalConfig.sampleRate;
      }
      return 0.0f;
    }

    float maximumDelayTime()
    {
      return (float)mMaxDelayInSamples / globalConfig.sampleRate;
    }

#ifndef SWIGLUA
    od::Inlet mIn{"In"};
    od::Outlet mOut{"Out"};
    od::Outlet mOutR{"OutR"};

    od::Parameter mSize{"Size", 0.5f};            // 0..1, scales max tap delay
    od::Parameter mDensity{"Density", 0.5f};      // 0..1, fraction of reflectors active
    od::Parameter mMotion{"Motion", 0.0f};        // 0..1, listener phase around orbit
    od::Parameter mDecay{"Decay", 0.5f};          // 0..1, feedback gain scaler
    od::Parameter mWet{"Wet", 0.5f};              // 0..1, dry/wet mix
    od::Parameter mInputLevel{"InputLevel", 1.0f};
    od::Parameter mSeed{"Seed", 0.0f};            // hashed to uint32 for field regen

    virtual void process()
    {
      const int maxDelay = mMaxDelayInSamples;
      if (!mBuffer || maxDelay <= 0)
      {
        // Pass through silence if not allocated yet.
        float *outL = mOut.buffer();
        float *outR = mOutR.buffer();
        memset(outL, 0, FRAMELENGTH * sizeof(float));
        memset(outR, 0, FRAMELENGTH * sizeof(float));
        return;
      }

      float *in = mIn.buffer();
      float *outL = mOut.buffer();
      float *outR = mOutR.buffer();
      int16_t *buf = (int16_t *)mBuffer;

      // ---- Block-rate parameter reads + clamps ----
      float sizeNorm = mSize.value();
      if (!(sizeNorm >= 0.0f)) sizeNorm = 0.0f;
      if (sizeNorm > 1.0f) sizeNorm = 1.0f;

      float density = mDensity.value();
      if (!(density >= 0.0f)) density = 0.0f;
      if (density > 1.0f) density = 1.0f;
      // Always at least 1 active tap so the unit isn't completely silent.
      int activeTaps = (int)(density * kMaxNetworkTaps + 0.5f);
      if (activeTaps < 1) activeTaps = 1;
      if (activeTaps > kMaxNetworkTaps) activeTaps = kMaxNetworkTaps;

      float motion = mMotion.value();
      // motion wraps modulo 1; allow negative for reverse orbit.
      motion -= floorf(motion);

      float decay = mDecay.value();
      if (!(decay >= 0.0f)) decay = 0.0f;
      if (decay > 0.95f) decay = 0.95f;

      float wet = mWet.value();
      if (!(wet >= 0.0f)) wet = 0.0f;
      if (wet > 1.0f) wet = 1.0f;

      float inputLevel = mInputLevel.value();

      // ---- Seed dirty-check: regenerate field if changed ----
      // Hash the seed parameter (float) to a uint32. Simple bit-cast.
      union { float f; uint32_t u; } seedHash;
      seedHash.f = mSeed.value() + 1.0f;     // +1 so seed=0 still gives non-zero hash
      const uint32_t seedU = seedHash.u ^ 0x5A5A5A5Au;
      if (seedU != mLastSeed)
      {
        network_geom::regenerateField(mReflectors, kMaxNetworkTaps, seedU);
        mLastSeed = seedU;
      }

      // ---- Block-rate geometry recompute ----
      network_geom::recomputeTaps(
        mReflectors,
        kMaxNetworkTaps,
        activeTaps,
        sizeNorm,
        motion,
        maxDelay,
        1.0f,                          // gainScale
        mTapDelayTarget,
        mTapGainL,
        mTapGainR);

      // ---- Per-sample LP smoother coefficient ----
      // ~25ms time constant matches Pecto.cpp:486
      // (feedback_doppler_basedelay_smoother). Same alpha used for
      // delay, gainL, gainR smoothers.
      const float smoothAlpha = 1.0f / (0.025f * globalConfig.sampleRate);

      // First-block snap: prime the smoothed arrays to current targets
      // so the unit doesn't sweep audibly from 0 to target on insert.
      // Plain inline loop (no noinline helper) — tries to avoid the
      // crash signature the previous Phase 1.alpha bisect localized.
      if (mFirstProcess)
      {
        for (int t = 0; t < kMaxNetworkTaps; t++)
        {
          mTapDelaySmoothed[t] = mTapDelayTarget[t];
          mTapGainLSmoothed[t] = mTapGainL[t];
          mTapGainRSmoothed[t] = mTapGainR[t];
        }
        mFirstProcess = false;
      }

      // ---- Scratch arrays for 3-pass tap processing ----
      int32_t idx0[kMaxNetworkTaps];
      int32_t idx1[kMaxNetworkTaps];
      float frac[kMaxNetworkTaps];
      int16_t sA[kMaxNetworkTaps];
      int16_t sB[kMaxNetworkTaps];
      const float scale = 1.0f / 32767.0f;

#ifdef NETWORK_HAS_NEON
      const float32x4_t alphaVec = vdupq_n_f32(smoothAlpha);
#endif

      // ---- Per-sample inner loop ----
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        const float x = in[i] * inputLevel;

        if (mWriteIndex >= maxDelay) mWriteIndex = 0;

        const float writeIdxF = (float)mWriteIndex;
        const float maxDelayF = (float)maxDelay;

        // ---- Per-sample smoother step ----
        // One-pole LP on each per-tap target → smoothed. Iterates over
        // kMaxNetworkTaps so taps fading from inactive→active (or
        // vice-versa as density changes) get smooth transitions in
        // gain (target=0 for inactive). Pass A/C read smoothed arrays
        // below.
#ifdef NETWORK_HAS_NEON
        {
          int t = 0;
          for (; t + 4 <= kMaxNetworkTaps; t += 4)
          {
            // Tap delay
            float32x4_t tgt = vld1q_f32(&mTapDelayTarget[t]);
            float32x4_t sm  = vld1q_f32(&mTapDelaySmoothed[t]);
            sm = vmlaq_f32(sm, vsubq_f32(tgt, sm), alphaVec);
            vst1q_f32(&mTapDelaySmoothed[t], sm);

            // Gain L
            tgt = vld1q_f32(&mTapGainL[t]);
            sm  = vld1q_f32(&mTapGainLSmoothed[t]);
            sm = vmlaq_f32(sm, vsubq_f32(tgt, sm), alphaVec);
            vst1q_f32(&mTapGainLSmoothed[t], sm);

            // Gain R
            tgt = vld1q_f32(&mTapGainR[t]);
            sm  = vld1q_f32(&mTapGainRSmoothed[t]);
            sm = vmlaq_f32(sm, vsubq_f32(tgt, sm), alphaVec);
            vst1q_f32(&mTapGainRSmoothed[t], sm);
          }
        }
#else
        for (int t = 0; t < kMaxNetworkTaps; t++)
        {
          mTapDelaySmoothed[t] += (mTapDelayTarget[t] - mTapDelaySmoothed[t]) * smoothAlpha;
          mTapGainLSmoothed[t] += (mTapGainL[t] - mTapGainLSmoothed[t]) * smoothAlpha;
          mTapGainRSmoothed[t] += (mTapGainR[t] - mTapGainRSmoothed[t]) * smoothAlpha;
        }
#endif
#ifdef NETWORK_HAS_NEON
        // ---- Pass A (NEON): compute idx0 / idx1 / frac per tap ----
        {
          const float32x4_t writeIdxVec = vdupq_n_f32(writeIdxF);
          const float32x4_t zeroVec = vdupq_n_f32(0.0f);
          const float32x4_t maxDelayFVec = vdupq_n_f32(maxDelayF);
          const int32x4_t maxDelayVec = vdupq_n_s32(maxDelay);
          const int32x4_t oneVec = vdupq_n_s32(1);
          const int32x4_t zeroIVec = vdupq_n_s32(0);

          int t = 0;
          for (; t + 4 <= activeTaps; t += 4)
          {
            float32x4_t delay = vld1q_f32(&mTapDelaySmoothed[t]);
            float32x4_t p = vsubq_f32(writeIdxVec, delay);
            uint32x4_t negMask = vcltq_f32(p, zeroVec);
            float32x4_t pWrap = vaddq_f32(p, maxDelayFVec);
            p = vbslq_f32(negMask, pWrap, p);
            int32x4_t i0v = vcvtq_s32_f32(p);
            // idx0 ulp-edge guard — feedback_multitap_idx_wrap_ulp.
            uint32x4_t i0WrapMask = vcgeq_s32(i0v, maxDelayVec);
            i0v = vbslq_s32(i0WrapMask, zeroIVec, i0v);
            int32x4_t i1v = vaddq_s32(i0v, oneVec);
            uint32x4_t i1WrapMask = vcgeq_s32(i1v, maxDelayVec);
            i1v = vbslq_s32(i1WrapMask, zeroIVec, i1v);
            float32x4_t fracV = vsubq_f32(p, vcvtq_f32_s32(i0v));
            vst1q_s32(&idx0[t], i0v);
            vst1q_s32(&idx1[t], i1v);
            vst1q_f32(&frac[t], fracV);
          }
          for (; t < activeTaps; t++)
          {
            float p = writeIdxF - mTapDelaySmoothed[t];
            if (p < 0.0f) p += maxDelayF;
            int i0 = (int)p;
            if (i0 >= maxDelay) i0 = 0;
            int i1 = i0 + 1;
            if (i1 >= maxDelay) i1 = 0;
            idx0[t] = i0;
            idx1[t] = i1;
            frac[t] = p - (float)i0;
          }
        }
#else
        for (int t = 0; t < activeTaps; t++)
        {
          float p = writeIdxF - mTapDelaySmoothed[t];
          if (p < 0.0f) p += maxDelayF;
          int i0 = (int)p;
          if (i0 >= maxDelay) i0 = 0;
          int i1 = i0 + 1;
          if (i1 >= maxDelay) i1 = 0;
          idx0[t] = i0;
          idx1[t] = i1;
          frac[t] = p - (float)i0;
        }
#endif

        // ---- Pass B (scalar gather + 8-ahead prefetch) ----
        for (int t = 0; t < activeTaps; t++)
        {
          int pfIdx = t + 8;
          if (pfIdx < activeTaps)
            __builtin_prefetch(&buf[idx0[pfIdx]], 0, 1);
          sA[t] = buf[idx0[t]];
          sB[t] = buf[idx1[t]];
        }

        float wetL = 0.0f;
        float wetR = 0.0f;
        float lastTapOut = 0.0f;

#ifdef NETWORK_HAS_NEON
        // ---- Pass C (NEON): interpolate + dual-FMA pan + accumulate ----
        // Two FMAs per tap: one into wetL with gainL, one into wetR
        // with gainR. Shared idx/frac arrays from Pass A.
        {
          const float32x4_t scaleVec = vdupq_n_f32(scale);
          float32x4_t wetLVec = vdupq_n_f32(0.0f);
          float32x4_t wetRVec = vdupq_n_f32(0.0f);
          int t = 0;
          for (; t + 4 <= activeTaps; t += 4)
          {
            int16x4_t sAi = vld1_s16(&sA[t]);
            int16x4_t sBi = vld1_s16(&sB[t]);
            float32x4_t aV = vmulq_f32(vcvtq_f32_s32(vmovl_s16(sAi)), scaleVec);
            float32x4_t bV = vmulq_f32(vcvtq_f32_s32(vmovl_s16(sBi)), scaleVec);
            float32x4_t fV = vld1q_f32(&frac[t]);
            float32x4_t gLV = vld1q_f32(&mTapGainLSmoothed[t]);
            float32x4_t gRV = vld1q_f32(&mTapGainRSmoothed[t]);
            float32x4_t tapV = vmlaq_f32(aV, vsubq_f32(bV, aV), fV);
            wetLVec = vmlaq_f32(wetLVec, tapV, gLV);
            wetRVec = vmlaq_f32(wetRVec, tapV, gRV);
            lastTapOut = vgetq_lane_f32(tapV, 3);
          }
          // Horizontal sum L.
          {
            float32x2_t loHi = vadd_f32(vget_low_f32(wetLVec), vget_high_f32(wetLVec));
            wetL = vget_lane_f32(vpadd_f32(loHi, loHi), 0);
          }
          // Horizontal sum R.
          {
            float32x2_t loHi = vadd_f32(vget_low_f32(wetRVec), vget_high_f32(wetRVec));
            wetR = vget_lane_f32(vpadd_f32(loHi, loHi), 0);
          }
          // Scalar tail.
          for (; t < activeTaps; t++)
          {
            float a = (float)sA[t] * scale;
            float b = (float)sB[t] * scale;
            float tapOut = a + (b - a) * frac[t];
            wetL += tapOut * mTapGainLSmoothed[t];
            wetR += tapOut * mTapGainRSmoothed[t];
            lastTapOut = tapOut;
          }
        }
#else
        for (int t = 0; t < activeTaps; t++)
        {
          float a = (float)sA[t] * scale;
          float b = (float)sB[t] * scale;
          float tapOut = a + (b - a) * frac[t];
          wetL += tapOut * mTapGainLSmoothed[t];
          wetR += tapOut * mTapGainRSmoothed[t];
          lastTapOut = tapOut;
        }
#endif

        // Single global feedback (Phase 0 pattern; Phase 2 replaces
        // with sparse per-tap weighted recycle).
        float fb = lastTapOut * decay;
        if (fb > 1.5f) fb = 1.5f;
        if (fb < -1.5f) fb = -1.5f;

        bufWrite(buf, mWriteIndex, x + fb);

        mWriteIndex++;
        if (mWriteIndex >= maxDelay) mWriteIndex = 0;

        // Mix.
        const float mixedL = x * (1.0f - wet) + wetL * wet;
        const float mixedR = x * (1.0f - wet) + wetR * wet;
        outL[i] = mixedL;
        outR[i] = mixedR;
      }

    }

    bool allocate(int Ns)
    {
      deallocate();
      const int nbytes = Ns * sizeof(int16_t);
      mBuffer = new (std::nothrow) char[nbytes];
      if (mBuffer)
        memset(mBuffer, 0, nbytes);
      return mBuffer != 0;
    }

    void deallocate()
    {
      if (mBuffer)
      {
        delete[] mBuffer;
        mBuffer = 0;
      }
    }
#endif

  private:
    int mMaxDelayInSamples = 0;
    int mWriteIndex = 0;
    bool mFirstProcess = true;
    uint32_t mLastSeed = 0;

    // Reflector field (deterministically seeded).
    network_geom::Reflector mReflectors[kMaxNetworkTaps];

    // Per-tap state. Class-member arrays per
    // feedback_neon_intrinsics_drumvoice (heap-allocated, alignment safe).
    //
    // Block-rate targets (recomputed from geometry each block):
    float mTapDelayTarget[kMaxNetworkTaps];
    float mTapGainL[kMaxNetworkTaps];
    float mTapGainR[kMaxNetworkTaps];
    // Per-sample LP-smoothed values (Pass A/C read these). One-pole
    // smoother with ~25ms time constant — same pattern as Pecto's
    // mSmoothedBaseDelay (feedback_doppler_basedelay_smoother) but
    // per-tap instead of global.
    float mTapDelaySmoothed[kMaxNetworkTaps];
    float mTapGainLSmoothed[kMaxNetworkTaps];
    float mTapGainRSmoothed[kMaxNetworkTaps];

    // Delay buffer.
    char *mBuffer = 0;

#ifndef SWIGLUA
    static inline void bufWrite(int16_t *buf, int idx, float v)
    {
      int s = (int)(v * 32767.0f);
      if (s > 32767) s = 32767;
      if (s < -32767) s = -32767;
      buf[idx] = (int16_t)s;
    }
#endif
  };

} // namespace stolmine
