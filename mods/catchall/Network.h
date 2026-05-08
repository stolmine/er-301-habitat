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

  // Allpass diffusion stage delay lengths (samples). Primes for max
  // decorrelation between stages — no shared resonant subharmonics.
  // Total chain length ~12ms at 48kHz. Per-stage allpass formula:
  //   v[n] = x[n] + g·buf[n-D]
  //   y[n] = -g·v[n] + buf[n-D]
  //   buf[n] = v[n]
  // Phase scrambled, magnitude spectrum unchanged. Schroeder/Dattorro
  // pattern — directly addresses feedback-loop resonance accumulation
  // by phase-decorrelating the recycled signal each cycle.
  static const int kNetworkAp1Len = 53;
  static const int kNetworkAp2Len = 97;
  static const int kNetworkAp3Len = 167;
  static const int kNetworkAp4Len = 251;

  static inline float networkAllpassStep(float in, float *buf, int N, int &idx, float g)
  {
    const float bufVal = buf[idx];
    const float v = in + g * bufVal;
    const float out = -g * v + bufVal;
    buf[idx] = v;
    idx++;
    if (idx >= N) idx = 0;
    return out;
  }

  // DC-blocker pole coefficient (matches Pecto.cpp DC blocker, ≈7.6Hz
  // cutoff at 48kHz). Network applies three blockers: input, feedback
  // path, and stereo output. Without these, asymmetric tanh
  // saturation under sustained input + feedback latches the buffer
  // into DC offset (signal drifts past tanh's saturation range,
  // output becomes constant +1, speakers hear silence).
  static const float kNetworkDcR = 0.999f;

  // Fast tanh approximation (Padé 3/3). Lifted from Pecto.cpp:31-37.
  // Smooth, bounded ±1, monotonic — appropriate for feedback soft-
  // saturation where a hard clamp would kill DSP via a latched
  // saturated-buffer state and produce aliasing-rich harmonics that
  // re-energize the loop.
  static inline float networkFastTanh(float x)
  {
    if (x < -4.0f) return -1.0f;
    if (x >  4.0f) return  1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

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
      addParameter(mConnectivity);
      addParameter(mSoften);

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
        mFbWeight[i] = 0.0f;
        mFbWeightSmoothed[i] = 0.0f;
      }

      mWriteIndex = 0;
      mBuffer = 0;
      mMaxDelayInSamples = 0;
      mFirstProcess = true;

      // DC blocker state
      mDcInX1 = 0.0f; mDcInY1 = 0.0f;
      mDcFbX1 = 0.0f; mDcFbY1 = 0.0f;
      mDcOutLX1 = 0.0f; mDcOutLY1 = 0.0f;
      mDcOutRX1 = 0.0f; mDcOutRY1 = 0.0f;

      // Smooth-random listener walker (Phase 2 polish)
      mWalkerPos = 0.0f;
      mWalkerVel = 0.0f;
      mWalkerLcg = 0xCAFEBABEu;

      // Allpass diffusion buffers (4-stage Schroeder chain)
      memset(mApBuf1, 0, sizeof(mApBuf1));
      memset(mApBuf2, 0, sizeof(mApBuf2));
      memset(mApBuf3, 0, sizeof(mApBuf3));
      memset(mApBuf4, 0, sizeof(mApBuf4));
      mApIdx1 = 0;
      mApIdx2 = 0;
      mApIdx3 = 0;
      mApIdx4 = 0;
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
    od::Parameter mConnectivity{"Connectivity", 0.0f}; // 0..1, fraction of taps recycling
    od::Parameter mSoften{"Soften", 0.0f};        // 0..1, allpass diffusion in fb path
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

      // Motion now controls modulation DEPTH on a smooth-random walker
      // that drives the listener position. Continuous walker motion
      // breaks feedback-loop phase coherence so resonant peaks can't
      // accumulate. See walker advance below.
      float motionDepth = mMotion.value();
      if (!(motionDepth >= 0.0f)) motionDepth = 0.0f;
      if (motionDepth > 1.0f) motionDepth = 1.0f;

      float decay = mDecay.value();
      if (!(decay >= 0.0f)) decay = 0.0f;
      if (decay > 0.95f) decay = 0.95f;

      float connectivity = mConnectivity.value();
      if (!(connectivity >= 0.0f)) connectivity = 0.0f;
      if (connectivity > 1.0f) connectivity = 1.0f;

      float soften = mSoften.value();
      if (!(soften >= 0.0f)) soften = 0.0f;
      if (soften > 1.0f) soften = 1.0f;

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

      // ---- Walker advance (block-rate) ----
      // Smooth-random walker — listener drifts around the orbit
      // automatically, breaking phase coherence in the feedback loop.
      // Rate matrix: base 0.125Hz (8s period at full) × (1 + 4·conn·dcy)
      // — accelerates when feedback is hot, suppressing resonance
      // buildup at higher loop gain.
      mWalkerLcg = mWalkerLcg * 1103515245u + 12345u;
      const float velTarget =
        (float)((mWalkerLcg >> 16) & 0xFFFFu) * (2.0f / 65535.0f) - 1.0f;
      // Block-rate velocity smoother (~50ms time constant in equiv).
      const float blockVelAlpha =
        1.0f - expf(-(float)FRAMELENGTH / (0.05f * globalConfig.sampleRate));
      mWalkerVel += (velTarget - mWalkerVel) * blockVelAlpha;

      const float kBaseWalkerHz = 0.25f;             // 4s period
      const float matrixScale = 1.0f + 4.0f * connectivity * decay;
      const float walkerHz = kBaseWalkerHz * matrixScale;
      const float blockDt = (float)FRAMELENGTH / globalConfig.sampleRate;
      mWalkerPos += mWalkerVel * walkerHz * blockDt * motionDepth;
      mWalkerPos -= floorf(mWalkerPos);  // wrap to [0,1) (handles negatives)
      const float listenerMotion = mWalkerPos;

      // ---- Block-rate geometry recompute ----
      network_geom::recomputeTaps(
        mReflectors,
        kMaxNetworkTaps,
        activeTaps,
        sizeNorm,
        listenerMotion,
        maxDelay,
        1.0f,                          // gainScale
        mTapDelayTarget,
        mTapGainL,
        mTapGainR);

      // ---- Block-rate feedback selection (Phase 2) ----
      // Sparse selectable feedback recycling: pick k of activeTaps to
      // recycle into the write head. "Every-stride" allocation policy
      // — selected taps are spread across the active range so the
      // recycled signal has temporal diversity rather than clustering
      // at the closest-N reflectors.
      //
      // Normalization: 1/sqrt(k) per tap. With decorrelated tap
      // delays (phyllotaxis distribution), the RMS feedback amplitude
      // is `decay` regardless of k — comparable to a single-tap
      // recycle. Worst-case constructive sum is sqrt(k)*decay; the
      // ±1.5 hard clamp on `fb` below catches phase-alignment spikes
      // that occur statistically.
      const int kRecycle = (int)(connectivity * (float)activeTaps + 0.5f);
      const float fbWeightUnit = (kRecycle > 0)
                                   ? (decay / sqrtf((float)kRecycle))
                                   : 0.0f;
      // Zero all targets, then mark selected taps.
      // Sign randomization: each selected tap gets ±fbWeightUnit
      // determined by a deterministic hash of (t, mLastSeed). Mixing
      // signs breaks coherent constructive buildup at resonant
      // frequencies — the comb-filter peaks that produced ringing
      // become statistically zero. RMS feedback level unchanged
      // (sqrt(k) scaling holds for both signed and unsigned random
      // walks). Different seeds → different sign patterns, so
      // randomizing seed sweeps through different resonance
      // configurations.
      for (int t = 0; t < kMaxNetworkTaps; t++) mFbWeight[t] = 0.0f;
      if (kRecycle > 0)
      {
        const float ratio = (float)activeTaps / (float)kRecycle;
        for (int n = 0; n < kRecycle; n++)
        {
          int t = (int)(n * ratio);
          if (t >= activeTaps) t = activeTaps - 1;
          // Deterministic ±1 sign from (t, seed) hash. Knuth golden
          // ratio multiplier × LCG step → reasonably random middle
          // bits. Bit 16 chosen for stable distribution.
          uint32_t h = mLastSeed ^ ((uint32_t)t * 2654435761u);
          h = h * 1103515245u + 12345u;
          const float sign = ((h >> 16) & 1u) ? 1.0f : -1.0f;
          mFbWeight[t] = sign * fbWeightUnit;
        }
      }

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
          mFbWeightSmoothed[t] = mFbWeight[t];
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
        // Input DC blocker: y[n] = x[n] - x[n-1] + R*y[n-1]. Removes
        // any DC the user might patch in; protects feedback loop.
        const float xRaw = in[i] * inputLevel;
        const float x = xRaw - mDcInX1 + kNetworkDcR * mDcInY1;
        mDcInX1 = xRaw;
        mDcInY1 = x;

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

            // Feedback weight (Phase 2)
            tgt = vld1q_f32(&mFbWeight[t]);
            sm  = vld1q_f32(&mFbWeightSmoothed[t]);
            sm = vmlaq_f32(sm, vsubq_f32(tgt, sm), alphaVec);
            vst1q_f32(&mFbWeightSmoothed[t], sm);
          }
        }
#else
        for (int t = 0; t < kMaxNetworkTaps; t++)
        {
          mTapDelaySmoothed[t] += (mTapDelayTarget[t] - mTapDelaySmoothed[t]) * smoothAlpha;
          mTapGainLSmoothed[t] += (mTapGainL[t] - mTapGainLSmoothed[t]) * smoothAlpha;
          mTapGainRSmoothed[t] += (mTapGainR[t] - mTapGainRSmoothed[t]) * smoothAlpha;
          mFbWeightSmoothed[t] += (mFbWeight[t] - mFbWeightSmoothed[t]) * smoothAlpha;
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
        float fbSum = 0.0f;

#ifdef NETWORK_HAS_NEON
        // ---- Pass C (NEON): interpolate + triple-FMA + accumulate ----
        // Three FMAs per tap: into wetL (with gainLSmoothed), wetR
        // (with gainRSmoothed), and fbSum (with fbWeightSmoothed).
        // Shared idx/frac arrays from Pass A.
        {
          const float32x4_t scaleVec = vdupq_n_f32(scale);
          float32x4_t wetLVec = vdupq_n_f32(0.0f);
          float32x4_t wetRVec = vdupq_n_f32(0.0f);
          float32x4_t fbVec   = vdupq_n_f32(0.0f);
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
            float32x4_t fbWV = vld1q_f32(&mFbWeightSmoothed[t]);
            float32x4_t tapV = vmlaq_f32(aV, vsubq_f32(bV, aV), fV);
            wetLVec = vmlaq_f32(wetLVec, tapV, gLV);
            wetRVec = vmlaq_f32(wetRVec, tapV, gRV);
            fbVec   = vmlaq_f32(fbVec,   tapV, fbWV);
          }
          // Horizontal sums.
          {
            float32x2_t loHi = vadd_f32(vget_low_f32(wetLVec), vget_high_f32(wetLVec));
            wetL = vget_lane_f32(vpadd_f32(loHi, loHi), 0);
          }
          {
            float32x2_t loHi = vadd_f32(vget_low_f32(wetRVec), vget_high_f32(wetRVec));
            wetR = vget_lane_f32(vpadd_f32(loHi, loHi), 0);
          }
          {
            float32x2_t loHi = vadd_f32(vget_low_f32(fbVec), vget_high_f32(fbVec));
            fbSum = vget_lane_f32(vpadd_f32(loHi, loHi), 0);
          }
          // Scalar tail.
          for (; t < activeTaps; t++)
          {
            float a = (float)sA[t] * scale;
            float b = (float)sB[t] * scale;
            float tapOut = a + (b - a) * frac[t];
            wetL += tapOut * mTapGainLSmoothed[t];
            wetR += tapOut * mTapGainRSmoothed[t];
            fbSum += tapOut * mFbWeightSmoothed[t];
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
          fbSum += tapOut * mFbWeightSmoothed[t];
        }
#endif

        // Sparse feedback recycle (Phase 2): weighted sum of selected
        // tap outputs, normalized by 1/sqrt(k). Soft-clip via tanh,
        // then DC-block. The DC blocker is critical here — sustained
        // asymmetric tanh saturation accumulates DC into the loop
        // unless removed each cycle.
        const float fbTanh = networkFastTanh(fbSum);
        const float fbDc = fbTanh - mDcFbX1 + kNetworkDcR * mDcFbY1;
        mDcFbX1 = fbTanh;
        mDcFbY1 = fbDc;

        // Allpass diffusion chain (4 stages, Schroeder pattern).
        // Phase-decorrelates the recycled signal each cycle. Resonant
        // accumulation can't outpace this because the spectral
        // redistribution happens INSTANTLY each loop, not over a slow
        // walker timescale. Mixed by `soften` parameter — at 0 the
        // raw fb is used (preserves glitch / event identity); at 1
        // full diffusion (lush reverb cloud).
        const float kApG = 0.6f;
        float diffused = networkAllpassStep(fbDc,    mApBuf1, kNetworkAp1Len, mApIdx1, kApG);
        diffused       = networkAllpassStep(diffused, mApBuf2, kNetworkAp2Len, mApIdx2, kApG);
        diffused       = networkAllpassStep(diffused, mApBuf3, kNetworkAp3Len, mApIdx3, kApG);
        diffused       = networkAllpassStep(diffused, mApBuf4, kNetworkAp4Len, mApIdx4, kApG);
        const float fb = fbDc + soften * (diffused - fbDc);

        // Buffer-write soft saturation: x + fb can reach ±2, but the
        // int16 delay buffer storage clips hard at ±1. Apply tanh
        // again at write so accumulated input + feedback stays in
        // [-1, +1] smoothly. Each feedback cycle contributes gentle
        // tape-style compression rather than digital clipping.
        bufWrite(buf, mWriteIndex, networkFastTanh(x + fb));

        mWriteIndex++;
        if (mWriteIndex >= maxDelay) mWriteIndex = 0;

        // Mix.
        const float mixedL = x * (1.0f - wet) + wetL * wet;
        const float mixedR = x * (1.0f - wet) + wetR * wet;

        // Output DC blockers (stereo). Catches any DC slipping
        // through wet from the wet path's own saturation residue.
        const float outDcL = mixedL - mDcOutLX1 + kNetworkDcR * mDcOutLY1;
        mDcOutLX1 = mixedL;
        mDcOutLY1 = outDcL;
        const float outDcR = mixedR - mDcOutRX1 + kNetworkDcR * mDcOutRY1;
        mDcOutRX1 = mixedR;
        mDcOutRY1 = outDcR;

        outL[i] = outDcL;
        outR[i] = outDcR;
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
    // Phase 2: per-tap feedback weight (target + smoothed).
    float mFbWeight[kMaxNetworkTaps];
    float mFbWeightSmoothed[kMaxNetworkTaps];

    // DC blocker state (one-pole). Three blockers: input, feedback,
    // stereo output. Prevents DC drift from asymmetric tanh
    // saturation under sustained feedback (drift latches buffer into
    // saturated state, kills DSP).
    float mDcInX1, mDcInY1;
    float mDcFbX1, mDcFbY1;
    float mDcOutLX1, mDcOutLY1;
    float mDcOutRX1, mDcOutRY1;

    // Smooth-random listener walker. Replaces direct motion-as-phase
    // with motion-as-depth: the walker continuously wanders around
    // the orbit, breaking feedback-loop phase coherence so resonant
    // peaks can't accumulate. Rate is matrix-driven from connectivity
    // × decay (faster modulation when the loop is hot).
    float mWalkerPos;        // [0, 1) — current orbit phase
    float mWalkerVel;        // smoothed velocity, ~[-1, +1]
    uint32_t mWalkerLcg;     // deterministic random source

    // 4-stage allpass diffusion chain in feedback path. Phase-
    // decorrelates the recycled signal each cycle, breaking the
    // resonance accumulation that the walker alone can't outpace.
    float mApBuf1[kNetworkAp1Len];
    float mApBuf2[kNetworkAp2Len];
    float mApBuf3[kNetworkAp3Len];
    float mApBuf4[kNetworkAp4Len];
    int mApIdx1, mApIdx2, mApIdx3, mApIdx4;

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
