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

  // Serial cascade group constants (Phase A scaffolding for the
  // cascade rebuild — see planning/network-cascade-rebuild-plan.md).
  // Taps live in groups of 4 (NEON-native); signal flows through
  // groups in distance order. State members below this class are
  // staged but not yet wired into the per-sample loop.
  static const int kNetworkGroupSize     = 4;
  static const int kNetworkNumGroupsMax  =
    kMaxNetworkTaps / kNetworkGroupSize;   // 16

  // Per-tap glitch effect mode (mutex). Each tap has at most one
  // glitch effect per cycle. Effects still stack across taps via
  // the shared feedback bus, so high-glitch settings still produce
  // layered character — just not multiple effects on the same tap.
  enum NetworkTapMode : uint8_t
  {
    NETWORK_TAP_NORMAL  = 0,
    NETWORK_TAP_MUTE    = 1,
    NETWORK_TAP_STUTTER = 2,
    NETWORK_TAP_CRUSH   = 3,
    NETWORK_TAP_SCRUB   = 4,
    NETWORK_TAP_REVERSE = 5
  };

  // Allpass diffusion stage delay lengths (samples). Primes for max
  // decorrelation between stages — no shared resonant subharmonics.
  // Total chain length ~32ms at 48kHz (matches mid-range Schroeder
  // diffuser; previous 12ms was too short for noticeable spectral
  // smearing below ~1kHz). Per-stage allpass formula:
  //   v[n] = x[n] + g·buf[n-D]
  //   y[n] = -g·v[n] + buf[n-D]
  //   buf[n] = v[n]
  // Phase scrambled, magnitude spectrum unchanged. Schroeder/Dattorro
  // pattern — directly addresses feedback-loop resonance accumulation
  // by phase-decorrelating the recycled signal each cycle.
  static const int kNetworkAp1Len = 167;
  static const int kNetworkAp2Len = 263;
  static const int kNetworkAp3Len = 419;
  static const int kNetworkAp4Len = 677;

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

  // High-pass coefficient (~50Hz cutoff at 48kHz: R = 1 - 2π·50/48000).
  // Network applies three HPFs: input, feedback path, and stereo
  // output. Without these, asymmetric tanh saturation under
  // sustained feedback latches the buffer into DC offset.
  // Bumped from 0.999 (~7.6Hz cutoff) to suppress sub-bass standing
  // waves that accrue in low-frequency reaches of the feedback loop.
  static const float kNetworkDcR = 0.9935f;

  // Sign-change zero-crossing search in a circular int16 buffer.
  // Returns the index of the nearest zero crossing within ±range
  // around `center`. Algorithm follows
  // od::Grain::snapToZeroCrossing — search forward and backward
  // for first sign change (rising or falling), pick whichever
  // direction is closer; among the two samples straddling the
  // crossing, pick the one closer to zero. Falls back to
  // min-magnitude search if no sign change is found in the range
  // (e.g., DC-dominated signals); preserves the previous
  // 0.3.43-era behavior as a safety net.
  static inline int networkFindNearestZeroCrossing(
    int center, int range, const int16_t *buf, int maxDelay)
  {
    int forward = -1, forwardDist = range + 1;
    int backward = -1, backwardDist = range + 1;

    // Forward: walk +1, detect sign change between prev and cur.
    {
      int idx = center;
      int16_t prev = buf[idx];
      for (int off = 1; off <= range; off++)
      {
        idx = center + off;
        if (idx >= maxDelay) idx -= maxDelay;
        const int16_t cur = buf[idx];
        if ((prev <= 0 && cur > 0) || (prev >= 0 && cur < 0))
        {
          int prevIdx = idx - 1;
          if (prevIdx < 0) prevIdx += maxDelay;
          forward = (abs((int)prev) < abs((int)cur)) ? prevIdx : idx;
          forwardDist = off;
          break;
        }
        prev = cur;
      }
    }

    // Backward: walk -1, same logic.
    {
      int idx = center;
      int16_t prev = buf[idx];
      for (int off = 1; off <= range; off++)
      {
        idx = center - off;
        if (idx < 0) idx += maxDelay;
        const int16_t cur = buf[idx];
        if ((cur <= 0 && prev > 0) || (cur >= 0 && prev < 0))
        {
          int nextIdx = idx + 1;
          if (nextIdx >= maxDelay) nextIdx -= maxDelay;
          backward = (abs((int)prev) < abs((int)cur)) ? nextIdx : idx;
          backwardDist = off;
          break;
        }
        prev = cur;
      }
    }

    if (forward >= 0 && forwardDist <= backwardDist) return forward;
    if (backward >= 0)                                return backward;

    // Fallback: min-magnitude search.
    int bestIdx = center;
    int bestMag = abs((int)buf[center]);
    for (int off = 1; off <= range && bestMag > 0; off++)
    {
      for (int sg = -1; sg <= 1; sg += 2)
      {
        int i = center + sg * off;
        while (i < 0)         i += maxDelay;
        while (i >= maxDelay) i -= maxDelay;
        const int mag = abs((int)buf[i]);
        if (mag < bestMag) { bestMag = mag; bestIdx = i; }
      }
    }
    return bestIdx;
  }

  // Triangular distribution sampler. Maps a uniform [0,1] hash to a
  // [0,1] value with density peak at `mode`. Both extremes always
  // reachable; just less frequent the further from `mode`. Used by
  // glitch primitives to weight per-event continuum-distributed
  // values (loop length, duration, crush severity, ...) toward an
  // anchor parameter while keeping the full range available
  // regardless of where the anchor sits.
  // One sqrtf per call; called only on rare events (block-rate per
  // crushed tap or per stutter trigger), so cheap on Cortex-A8.
  static inline float networkTriangularSample(float u, float mode)
  {
    if (u < mode) return sqrtf(u * mode);
    return 1.0f - sqrtf((1.0f - u) * (1.0f - mode));
  }

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
      addParameter(mGlitch);

      // Initial reflector field at default seed.
      mLastSeed = 0xC0FFEE17u;
      network_geom::regenerateField(mReflectors, kMaxNetworkTaps, mLastSeed);

      // Zero per-tap state.
      for (int i = 0; i < kMaxNetworkTaps; i++)
      {
        mTapDelayTarget[i] = 0.0f;
        mTapGainL[i] = 0.0f;
        mTapGainR[i] = 0.0f;
        mTapGainLSmoothed[i] = 0.0f;
        mTapGainRSmoothed[i] = 0.0f;
        mFbWeight[i] = 0.0f;
        mFbWeightSmoothed[i] = 0.0f;
        mTapOldReadIdx[i] = 0;
        mTapNewReadIdx[i] = 0;
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

      // Per-tap shimmer LFO (S2 lush half — rate varies per tap)
      for (int i = 0; i < kMaxNetworkTaps; i++)
      {
        mLfoPhase[i] = 0.0f;
        mLfoRate[i] = 0.5f;       // initial uniform; recomputed each block
        mTapLpState[i] = 0.0f;    // L3 LP state
        mTapLpCoeff[i] = 0.5f;    // initial; recomputed each block
        mTapShClock[i] = 0.0f;    // G1 S&H clock phase
        mTapShValue[i] = 0.0f;    // G1 S&H captured snapshot (samples)
        mTapEffectMode[i] = NETWORK_TAP_NORMAL;
        mTapReadAdvance[i] = 1;
        mTapStutterIterations[i] = 0;
        mTapStutterAnchor[i] = 0;
        mTapStutterLength[i] = 0;
        mTapStutterLoopSamples[i] = 0.0f;
        mTapStutterReadPtr[i] = 0.0f;
        mTapStutterPosInLoop[i] = 0.0f;
        mTapStutterSpeed[i] = 1.0f;
        mTapStutterGainL[i] = 0.0f;
        mTapStutterGainR[i] = 0.0f;
        mTapStutterFbW[i] = 0.0f;
        mTapCrushMask[i] = 0.0f;      // G8 crush: 0 = un-crushed
        mTapCrushBitLvl[i] = 1.0f;    // identity bitcrush
        mTapCrushInvBitLvl[i] = 1.0f;
        mTapDecimFactorF[i] = 1.0f;   // factor=1 → decimate identity
        mTapDecimCounterF[i] = 0.0f;
        mTapDecimHold[i] = 0.0f;
      }

      // Glitch macro RNG state — independent of mWalkerLcg so glitch
      // event timing doesn't lock to motion phase.
      mGlitchLcg = 0xFEEDF00Du;

      // G4 transient detector — start envelopes at 0; cooldown 0.
      mEnvFast = 0.0f;
      mEnvSlow = 0.0f;
      mTransientCooldown = 0;

      // G7 respawn — zero life means "needs initial seeding" on
      // first motion×glitch>0 block.
      for (int i = 0; i < kMaxNetworkTaps; i++)
      {
        mTapLifeRemaining[i] = 0;
      }

      // Graphic-side accessors state.
      mLastActiveTaps = 0;
      mLastSizeNorm = 0.5f;
      mLastDecay = 0.5f;
      mLastMotion = 0.0f;
      for (int i = 0; i < kMaxNetworkTaps; i++)
      {
        mTapRicochetFlash[i] = 0;
        mTapGeomReadIdx[i] = 0;
      }
      for (int i = 0; i < kOutputRingSize; i++) mOutputRing[i] = 0.0f;
      mOutputRingPos = 0;
      for (int i = 0; i < kListenerTraceSize; i++) mListenerTrace[i] = 0.0f;
      mListenerTraceHead = 0;

      // Allpass diffusion buffers (4-stage Schroeder chain)
      memset(mApBuf1, 0, sizeof(mApBuf1));
      memset(mApBuf2, 0, sizeof(mApBuf2));
      memset(mApBuf3, 0, sizeof(mApBuf3));
      memset(mApBuf4, 0, sizeof(mApBuf4));
      mApIdx1 = 0;
      mApIdx2 = 0;
      mApIdx3 = 0;
      mApIdx4 = 0;

      // ---- Phase A cascade scaffolding init ----
      mCascadeSeed =
        (uint32_t)((uintptr_t)this * 2654435761u) ^ 0xCA5CADE1u;
      for (int g = 0; g < kNetworkNumGroupsMax; g++)
      {
        mGroupOrigin[g]        = 0;
        mGroupLen[g]           = 0;
        mGroupWriteIndex[g]    = 0;
        mGroupLocalFbState[g]  = 0.0f;
        mGroupMonoStutterAcc[g] = 0.0f;
        mGroupStutterWetL[g]    = 0.0f;
        mGroupStutterWetR[g]    = 0.0f;
        mGroupDcX1[g]          = 0.0f;
        mGroupDcY1[g]          = 0.0f;
        mGroupFbLpState[g]     = 0.0f;
        mGroupFbHpX1[g]        = 0.0f;
        mGroupFbHpY1[g]        = 0.0f;
        mGroupPoolSign[g]      = 1.0f;
        mHadamardScratch[g]    = 0.0f;
        mGroupDecayCoef[g]     = 0.0f;
      }
      mDiffusedGlobalPool = 0.0f;
      mPoolHpX1 = 0.0f;
      mPoolHpY1 = 0.0f;
      // mTapGroupMap / mTapGroupSlot / mTapIntraGroupOffset
      // populated by recomputeCascadeAssignment() — defer to first
      // process() call (depends on mReflectors being seeded).
      for (int t = 0; t < kMaxNetworkTaps; t++)
      {
        mTapGroupMap[t]          = 0;
        mTapGroupSlot[t]         = 0;
        mTapIntraGroupOffset[t]  = 0.5f;
      }
      for (int s = 0; s < kStutScratchSlots; s++) mStutGroup[s] = 0;

      // Compute initial cascade assignment from default reflectors.
      // Reflectors are seeded above via regenerateField; the
      // seed-dirty check in process() will re-run this on user
      // seed changes.
      recomputeCascadeAssignment();
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

    // ---- Read-only accessors for the overview graphic ----
    // SWIG-visible inline; no out-of-line virtuals concern.
    int getActiveTapCount() const { return mLastActiveTaps; }
    int getMaxDelayInSamples() const { return mMaxDelayInSamples; }
    float getReflectorX(int t) const
    {
      return (t >= 0 && t < kMaxNetworkTaps) ? mReflectors[t].x : 0.0f;
    }
    float getReflectorY(int t) const
    {
      return (t >= 0 && t < kMaxNetworkTaps) ? mReflectors[t].y : 0.0f;
    }
    int getTapMode(int t) const
    {
      return (t >= 0 && t < kMaxNetworkTaps) ? (int)mTapEffectMode[t] : 0;
    }
    int getTapStutterIter(int t) const
    {
      return (t >= 0 && t < kMaxNetworkTaps) ? (int)mTapStutterIterations[t] : 0;
    }
    // Returns posInLoop / loopSamples in [0, 1), or 0 if not stuttering.
    float getTapStutterPosNorm(int t) const
    {
      if (t < 0 || t >= kMaxNetworkTaps) return 0.0f;
      const float ls = mTapStutterLoopSamples[t];
      if (ls <= 0.0f) return 0.0f;
      return mTapStutterPosInLoop[t] / ls;
    }
    float getTapCrushMask(int t) const
    {
      return (t >= 0 && t < kMaxNetworkTaps) ? mTapCrushMask[t] : 0.0f;
    }
    float getTapDecimFactor(int t) const
    {
      return (t >= 0 && t < kMaxNetworkTaps) ? mTapDecimFactorF[t] : 1.0f;
    }
    int getRicochetFlash(int t) const
    {
      return (t >= 0 && t < kMaxNetworkTaps) ? (int)mTapRicochetFlash[t] : 0;
    }
    int getRicochetFlashMax() const { return (int)kRicochetFlashMax; }
    float getListenerPhase() const { return mWalkerPos; }
    float getSizeNorm() const { return mLastSizeNorm; }
    float getDecayNorm() const { return mLastDecay; }
    float getMotionNorm() const { return mLastMotion; }
    // idx in [0, 256); 0 = oldest valid sample, 255 = most recent.
    float getOutputSample(int idx) const
    {
      if (idx < 0 || idx >= kOutputRingSize) return 0.0f;
      return mOutputRing[(mOutputRingPos + idx) & (kOutputRingSize - 1)];
    }
    int getOutputRingSize() const { return kOutputRingSize; }
    // idx in [0, 128); 0 = oldest, 127 = most recent.
    float getListenerTracePhase(int idx) const
    {
      if (idx < 0 || idx >= kListenerTraceSize) return 0.0f;
      return mListenerTrace[(mListenerTraceHead + idx) & (kListenerTraceSize - 1)];
    }
    int getListenerTraceSize() const { return kListenerTraceSize; }
    float getFbWeight(int t) const
    {
      return (t >= 0 && t < kMaxNetworkTaps) ? mFbWeight[t] : 0.0f;
    }
    // Returns scrub offset in samples (signed, centered on 0).
    int getTapScrubOffset(int t) const
    {
      if (t < 0 || t >= kMaxNetworkTaps) return 0;
      const int maxD = mMaxDelayInSamples;
      if (maxD <= 0) return 0;
      int delta = mTapNewReadIdx[t] - mTapGeomReadIdx[t];
      // Wrap into signed range centered on 0.
      if (delta > maxD / 2) delta -= maxD;
      else if (delta < -maxD / 2) delta += maxD;
      return delta;
    }

#ifndef SWIGLUA
    od::Inlet mIn{"In"};
    od::Outlet mOut{"Out"};
    od::Outlet mOutR{"OutR"};

    od::Parameter mSize{"Size", 0.5f};            // 0..1, scales max tap delay
    od::Parameter mDensity{"Density", 0.5f};      // 0..1, fraction of reflectors active
    od::Parameter mMotion{"Motion", 0.0f};        // 0..1, listener phase around orbit
    od::Parameter mConnectivity{"Connectivity", 0.0f}; // 0..1, fraction of taps recycling
    // Soften (allpass diffusion in fb path) is now driven 1:1 by
    // connectivity — diffusion strength scales with feedback intensity
    // automatically. No separate ply.
    od::Parameter mDecay{"Decay", 0.5f};          // 0..1, feedback gain scaler
    od::Parameter mWet{"Wet", 0.5f};              // 0..1, dry/wet mix
    od::Parameter mInputLevel{"InputLevel", 1.0f};
    od::Parameter mSeed{"Seed", 0.0f};            // hashed to uint32 for field regen
    od::Parameter mGlitch{"Glitch", 0.0f};        // 0..1, Character macro: lush→glitch

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
      if (!(sizeNorm >= 0.01f)) sizeNorm = 0.01f;     // sub-1% causes
                                                      // major problems
                                                      // (taps collapse
                                                      // to 0 delay,
                                                      // direct fb path)
      if (sizeNorm > 1.0f) sizeNorm = 1.0f;
      mLastSizeNorm = sizeNorm;

      float density = mDensity.value();
      if (!(density >= 0.0f)) density = 0.0f;
      if (density > 1.0f) density = 1.0f;
      // Always at least 1 active tap so the unit isn't completely silent.
      int activeTaps = (int)(density * kMaxNetworkTaps + 0.5f);
      if (activeTaps < 1) activeTaps = 1;
      if (activeTaps > kMaxNetworkTaps) activeTaps = kMaxNetworkTaps;
      mLastActiveTaps = activeTaps;

      // Decay graphic ricochet flash counters (per-block decrement).
      for (int t = 0; t < kMaxNetworkTaps; t++)
      {
        if (mTapRicochetFlash[t] > 0) mTapRicochetFlash[t]--;
      }

      // Motion now controls modulation DEPTH on a smooth-random walker
      // that drives the listener position. Continuous walker motion
      // breaks feedback-loop phase coherence so resonant peaks can't
      // accumulate. See walker advance below.
      float motionDepth = mMotion.value();
      if (!(motionDepth >= 0.0f)) motionDepth = 0.0f;
      if (motionDepth > 1.0f) motionDepth = 1.0f;
      mLastMotion = motionDepth;

      float decay = mDecay.value();
      if (!(decay >= 0.0f)) decay = 0.0f;
      if (decay > 0.95f) decay = 0.95f;
      mLastDecay = decay;

      float connectivity = mConnectivity.value();
      if (!(connectivity >= 0.0f)) connectivity = 0.0f;
      if (connectivity > 1.0f) connectivity = 1.0f;

      // Soften ties 1:1 to connectivity — diffusion auto-scales with
      // feedback intensity (more recycling = more diffusion to suppress
      // resonance buildup). User experiences it as conn-dependent
      // smoothness, no separate knob.
      const float soften = connectivity;

      float wet = mWet.value();
      if (!(wet >= 0.0f)) wet = 0.0f;
      if (wet > 1.0f) wet = 1.0f;

      float glitchAmount = mGlitch.value();
      if (!(glitchAmount >= 0.0f)) glitchAmount = 0.0f;
      if (glitchAmount > 1.0f) glitchAmount = 1.0f;
      // Glitch RNG is reseeded on walker wrap (below, after walker
      // advance). Within a walker revolution, mGlitchLcg is held
      // constant — every per-tap glitch decision (mute mask, crush
      // subset, stutter trigger/length/duration) is deterministic
      // for the duration of the cycle. This produces a "locked
      // character" that holds for the cycle then shuffles on the
      // next revolution. Motion controls cycle frequency, so motion
      // controls how often glitch patterns shuffle.

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
        // Phase A: recompute cascade tap-to-group assignment when
        // reflectors change.
        recomputeCascadeAssignment();
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

      // Characteristic cycle period in blocks (assuming vel ≈ 1.0).
      // Used by G2 soft motion-cycle sync to bias stutter loop
      // lengths toward subdivisions of the walker cycle. Zero when
      // motion is too low to define a cycle.
      const float cyclePeriodBlocks = (motionDepth > 0.01f)
        ? (1.0f / (walkerHz * blockDt * motionDepth))
        : 0.0f;
      mWalkerPos += mWalkerVel * walkerHz * blockDt * motionDepth;
      // Wrap detection — walker revolutions drive global glitch
      // reseed (see below). At motion=0, walker is stationary, so
      // glitch state stays frozen for a "stuck character" cycle.
      // At high motion, frequent wraps → glitch patterns shuffle.
      const float walkerFloor = floorf(mWalkerPos);
      const bool walkerWrapped = (walkerFloor != 0.0f);
      mWalkerPos -= walkerFloor;        // wrap to [0,1) (handles negatives)
      const float listenerMotion = mWalkerPos;

      // Glitch reseed on walker wrap. Within a cycle, mGlitchLcg is
      // constant; on wrap, perturb to shuffle glitch patterns.
      // Combine with mLastSeed so changing the user seed also
      // changes the cycle-locked patterns.
      if (walkerWrapped)
      {
        mGlitchLcg = mGlitchLcg * 1103515245u + 12345u +
                     (mLastSeed ^ 0xDEADBEEFu);
      }

      // ---- G7 — tap respawn (lifetime-driven reflector reset) ----
      // Per-tap countdown in blocks. On expiration, the tap's
      // reflector position is re-hashed (new x,y in unit disk) and
      // life is reset to a random fraction of target. Respawn rate
      // scales with motion × glitch so motion=0 or glitch=0 stops
      // all respawns. Geometry recomputed below picks up the new
      // reflector positions.
      const float kMaxRespawnHz = 2.0f;        // max 2 respawns/sec/tap
      const float respawnRateHz = motionDepth * glitchAmount * kMaxRespawnHz;
      const int targetLifeBlocks = (respawnRateHz > 0.01f)
        ? (int)(1.0f / (respawnRateHz * blockDt))
        : 0;
      if (targetLifeBlocks > 0)
      {
        for (int t = 0; t < activeTaps; t++)
        {
          if (mTapLifeRemaining[t] == 0)
          {
            // Initial seeding — randomize so taps don't all respawn
            // simultaneously.
            uint32_t hLife = mGlitchLcg ^
              ((uint32_t)t * 2654435761u + 0xACE0BEEFu);
            hLife = hLife * 1103515245u + 12345u;
            const float lifeFrac =
              0.5f + (float)((hLife >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
            mTapLifeRemaining[t] =
              (uint16_t)((float)targetLifeBlocks * lifeFrac);
            continue;
          }
          mTapLifeRemaining[t]--;
          if (mTapLifeRemaining[t] == 0)
          {
            // Respawn — drift the reflector by a small random delta
            // (rather than fully re-randomizing). Big position jumps
            // produce audible 5ms morph artifacts via the dual-read
            // crossfade (which is only one block long) — full
            // re-randomization at high event rate sounds like a
            // continuous underrun stream. Delta of 0.15 in the unit
            // disk caps delay change to ~75ms worst case.
            const float kRespawnMaxDelta = 0.15f;
            uint32_t hPos = mGlitchLcg ^
              ((uint32_t)t * 2654435761u + 0x0CAFE0CDu);
            hPos = hPos * 1103515245u + 12345u;
            const float dx =
              ((float)((hPos >> 16) & 0xFFFFu) * (2.0f / 65535.0f)
               - 1.0f) * kRespawnMaxDelta;
            hPos = hPos * 1103515245u + 12345u;
            const float dy =
              ((float)((hPos >> 16) & 0xFFFFu) * (2.0f / 65535.0f)
               - 1.0f) * kRespawnMaxDelta;
            float xRaw = mReflectors[t].x + dx;
            float yRaw = mReflectors[t].y + dy;
            float r2 = xRaw * xRaw + yRaw * yRaw;
            if (r2 > 1.0f)
            {
              const float invR = 1.0f / sqrtf(r2);
              xRaw *= invR; yRaw *= invR;
            }
            mReflectors[t].x = xRaw;
            mReflectors[t].y = yRaw;
            // Reset life with hash variation so subsequent respawns
            // also stay desynchronized.
            hPos = hPos * 1103515245u + 12345u;
            const float lifeFrac =
              0.5f + (float)((hPos >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
            mTapLifeRemaining[t] =
              (uint16_t)((float)targetLifeBlocks * lifeFrac);
          }
        }
      }
      else
      {
        // Disabled — clear so reactivation re-seeds randomly.
        for (int t = 0; t < kMaxNetworkTaps; t++) mTapLifeRemaining[t] = 0;
      }

      // ---- Block-rate geometry recompute ----
      // Density-compensated gain: per-tap magnitude scales as
      // C × N^(-0.4) (slightly softer than the statistical 1/√N).
      // RMS isn't perfectly invariant — it grows as ~N^0.1 from
      // density=0→1, ~+3.5dB total, which preserves more presence
      // at high density without overdriving the tanh in the wet
      // bus. The fb path uses its own 1/√k normalization
      // independent of this constant.
      const float densityCompGain =
        2.5f * powf((float)activeTaps, -0.4f);
      network_geom::recomputeTaps(
        mReflectors,
        kMaxNetworkTaps,
        activeTaps,
        sizeNorm,
        listenerMotion,
        maxDelay,
        densityCompGain,
        mTapDelayTarget,
        mTapGainL,
        mTapGainR);

      // ---- Per-tap shimmer LFO (Rings-style chorus) ----
      // Rings's reverb (eurorack/rings/dsp/fx/reverb.h:111,123)
      // modulates its long delay reads with slow LFOs (0.3, 0.5 Hz)
      // at depth 40-50 samples (~1ms). This is what gives Rings its
      // characteristic shimmer and breaks comb-filter coherence in
      // the feedback loop. We apply the same idea: single global
      // LFO advanced at 0.5 Hz, per-tap phase offset via golden
      // angle so each tap wobbles independently. Mod depth small
      // (~8 samples / 0.17ms) — just enough to break coherence
      // without audible chorus pitch wobble.
      const float kLfoHz = 0.5f;
      const float kLfoDepthSamples = 8.0f;

      // S2: per-tap LFO rate spread. ±20% base + up to ±50% scaled
      // by motion. At motion=0 taps still have rate variation
      // (chorus baseline). At motion=1 rates diverge widely (true
      // polyphonic shimmer).
      const float kBaseRateSpread = 0.2f;
      const float kMotionRateSpread = 0.5f;
      const float rateSpread = kBaseRateSpread + kMotionRateSpread * motionDepth;

      // S1: per-tap pitch detune. Static delay offset hashed from
      // (t, mLastSeed), scaled by connectivity. Pragmatic substitute
      // for per-tap pitch shift that preserves the PDF's purpose
      // (destroy integer-ratio comb peaks via delay incoherence)
      // while integrating with the dual-read crossfading delay.
      // ±0.5ms (~24 samples at 48kHz) at full connectivity.
      const float kMaxPitchDetuneSamples = 24.0f;

      // L3: per-tap LP filter base coefficient. Maps decay 0..1
      // logarithmically to cutoff 18kHz..3kHz (Rings reverb damping
      // convention). Per-tap variation in the same loop below.
      const float kMaxCutoffHz = 18000.0f;
      // logf(6.0f) precomputed — eliminates a libm call per block.
      const float kLogCutoffRatio = 1.7917595f;
      const float baseCutoffHz =
        kMaxCutoffHz * expf(-decay * kLogCutoffRatio);
      const float kTwoPiOverSr =
        2.0f * 3.14159265358979f / globalConfig.sampleRate;
      const float baseCoeff =
        1.0f - expf(-baseCutoffHz * kTwoPiOverSr);

      // G1: S&H on tap positions. Clock rate scales with motion ×
      // glitch — minimum non-zero rate (1Hz) at glitch tip-on, up to
      // 16Hz at glitch=1 with full motion. At motion=0, clock is
      // halted and snapshots persist (held forever).
      const float kShMinClkHz = 1.0f;
      const float kShMaxClkHz = 16.0f;
      const float shClkHz = motionDepth * (kShMinClkHz +
                            (kShMaxClkHz - kShMinClkHz) * glitchAmount);
      const float shClkAdvance = shClkHz * blockDt;

      // (G3 mute is now mutex-assigned in the mode-selection pass
      //  below — this constant block is no longer needed.)

      for (int i = 0; i < activeTaps; i++)
      {
        // Per-tap LFO rate (S2). Hash for seeded per-tap variation
        // — different XOR mask from the S1 detune hash to keep
        // them uncorrelated.
        uint32_t hRate = mLastSeed ^ ((uint32_t)i * 2654435761u + 0x3C3C3C3Cu);
        hRate = hRate * 1103515245u + 12345u;
        const float rateOffset =
          ((float)((hRate >> 16) & 0xFFFFu) * (2.0f / 65535.0f)) - 1.0f;  // [-1, +1]
        mLfoRate[i] = kLfoHz * (1.0f + rateOffset * rateSpread);

        // Advance per-tap phase at its own rate.
        mLfoPhase[i] += mLfoRate[i] * blockDt;
        mLfoPhase[i] -= floorf(mLfoPhase[i]);

        // Existing shimmer modulation, now from per-tap phase.
        const float modOffset =
          network_trig::poly_sin(mLfoPhase[i]) * kLfoDepthSamples;

        // S1 per-tap pitch detune. Different XOR mask from the rate
        // hash to keep uncorrelated.
        uint32_t hDetune = mLastSeed ^ ((uint32_t)i * 2654435761u + 0xA5A5A5A5u);
        hDetune = hDetune * 1103515245u + 12345u;
        const float detuneSign = ((hDetune >> 16) & 1u) ? 1.0f : -1.0f;
        hDetune = hDetune * 1103515245u + 12345u;
        const float detuneFrac =
          (float)((hDetune >> 16) & 0xFFFFu) * (1.0f / 65535.0f);  // [0,1]
        const float pitchDetune = connectivity * kMaxPitchDetuneSamples
                                  * detuneSign * detuneFrac;

        mTapDelayTarget[i] += modOffset + pitchDetune;
        if (mTapDelayTarget[i] < 0.0f) mTapDelayTarget[i] = 0.0f;

        // G1 — S&H on tap positions. Snapshot on initial activation
        // (clock at 0 = freshly engaged or never wrapped) OR on clock
        // wrap. Lerp continuous→snapshot by glitchAmount: at
        // glitchAmount=0 we never enter this block; at glitchAmount=1
        // we replace continuous with snapshot entirely.
        if (glitchAmount > 0.0f)
        {
          const bool justActivated = (mTapShClock[i] == 0.0f);
          mTapShClock[i] += shClkAdvance;
          const bool wrapped = (mTapShClock[i] >= 1.0f);
          if (wrapped) mTapShClock[i] -= floorf(mTapShClock[i]);
          if (justActivated || wrapped)
            mTapShValue[i] = mTapDelayTarget[i];
          mTapDelayTarget[i] +=
            glitchAmount * (mTapShValue[i] - mTapDelayTarget[i]);
          if (mTapDelayTarget[i] < 0.0f) mTapDelayTarget[i] = 0.0f;
        }
        else
        {
          // Reset clock so the next re-engagement re-snapshots.
          mTapShClock[i] = 0.0f;
        }

        // (G3 mute applied later via mode-selection pass.)

        // L3 per-tap LP coefficient. ±30% cutoff variation around
        // the decay-driven base. Different XOR mask again to keep
        // uncorrelated with detune / rate hashes.
        uint32_t hLp = mLastSeed ^ ((uint32_t)i * 2654435761u + 0x77777777u);
        hLp = hLp * 1103515245u + 12345u;
        const float lpVariation =
          0.7f + 0.6f * ((float)((hLp >> 16) & 0xFFFFu) * (1.0f / 65535.0f));
        float coeff = baseCoeff * lpVariation;
        // Glitch attenuates LP — pulls coefficient toward 1 (less
        // filtering) so HF survives in feedback at high glitch,
        // making the texture brighter and grittier. At glitch=0
        // full LP; at glitch=1, ~30% of the original LP effect.
        coeff = coeff + glitchAmount * 0.7f * (1.0f - coeff);
        if (coeff > 1.0f) coeff = 1.0f;   // stability clamp
        if (coeff < 0.0f) coeff = 0.0f;
        mTapLpCoeff[i] = coeff;
      }

      // ---- Mutex mode assignment (block-rate, per-tap) ----
      // Single hash per tap → effect mode in {NORMAL, MUTE,
      // STUTTER, CRUSH, SCRUB, REVERSE} via cumulative probability
      // thresholds. Effects don't overlap on the same tap; they
      // still stack across taps via the shared feedback bus.
      //
      // Probability budget at glitch=1, density=1:
      //   pMute=0.15 + pStutter=0.40 + pCrush=0.25 + pScrub=0.15 +
      //   pReverse=0.15 = 1.10 (raw). Cumulative thresholds are
      //   capped at 65535 so overflow squeezes lowest-priority
      //   modes (REVERSE first, then SCRUB) without ever giving a
      //   higher-priority mode less than its budgeted share.
      // pNormal = remainder (~0–10% at full).
      const float kMaxMute    = 0.15f;
      const float kMaxStutter = 0.40f;
      // CRUSH / SCRUB / REVERSE / NORMAL split the remaining 45%
      // equally at glitch=1 (11.25% each). Stutter remains the
      // dominant glitch-mode character; mute is the second
      // dominant; the rest are evenly distributed flavors.
      const float kMaxCrush   = 0.1125f;
      const float kMaxScrub   = 0.1125f;
      const float kMaxReverse = 0.1125f;
      // All glitch-mode coverages are now purely probabilistic via
      // the glitch fader. Density no longer gates any glitch effect
      // — it only controls actual tap count (lush body) and feeds
      // densityCompGain for level compensation. Per-tap CRUSH
      // severities (bit depth, decimate factor) remain uniform
      // [0,1] hashes, also density-independent.
      const float pMute    = glitchAmount * kMaxMute;
      const float pStutter = glitchAmount * kMaxStutter;
      const float pCrush   = glitchAmount * kMaxCrush;
      const float pScrub   = glitchAmount * kMaxScrub;
      const float pReverse = glitchAmount * kMaxReverse;
      // Cumulative thresholds in 0..65535 unsigned space, capped.
      uint32_t muteThresh    = (uint32_t)(pMute * 65535.0f);
      if (muteThresh > 65535u) muteThresh = 65535u;
      uint32_t stutterThresh = muteThresh + (uint32_t)(pStutter * 65535.0f);
      if (stutterThresh > 65535u) stutterThresh = 65535u;
      uint32_t crushThresh   = stutterThresh + (uint32_t)(pCrush * 65535.0f);
      if (crushThresh > 65535u) crushThresh = 65535u;
      uint32_t scrubThresh   = crushThresh + (uint32_t)(pScrub * 65535.0f);
      if (scrubThresh > 65535u) scrubThresh = 65535u;
      uint32_t reverseThresh = scrubThresh + (uint32_t)(pReverse * 65535.0f);
      if (reverseThresh > 65535u) reverseThresh = 65535u;
      for (int i = 0; i < activeTaps; i++)
      {
        if (glitchAmount <= 0.0f)
        {
          mTapEffectMode[i] = NETWORK_TAP_NORMAL;
          mTapReadAdvance[i] = 1;
          continue;
        }
        uint32_t hMode = mGlitchLcg ^
          ((uint32_t)i * 2654435761u + 0xDD55DD55u);
        hMode = hMode * 1103515245u + 12345u;
        const uint32_t modeRand = (hMode >> 16) & 0xFFFFu;
        uint8_t mode = NETWORK_TAP_NORMAL;
        if      (modeRand < muteThresh)    mode = NETWORK_TAP_MUTE;
        else if (modeRand < stutterThresh) mode = NETWORK_TAP_STUTTER;
        else if (modeRand < crushThresh)   mode = NETWORK_TAP_CRUSH;
        else if (modeRand < scrubThresh)   mode = NETWORK_TAP_SCRUB;
        else if (modeRand < reverseThresh) mode = NETWORK_TAP_REVERSE;
        mTapEffectMode[i] = mode;
        mTapReadAdvance[i] = (mode == NETWORK_TAP_REVERSE) ? -1 : 1;
      }
      for (int i = activeTaps; i < kMaxNetworkTaps; i++)
      {
        mTapEffectMode[i] = NETWORK_TAP_NORMAL;
        mTapReadAdvance[i] = 1;
      }

      // ---- G3 mute (mode-gated) ----
      // MUTE-mode taps zero their gain AND fbWeight. Smoother
      // absorbs the L/R transition (~50ms ramp), no clicks.
      // (Previously fbWeight was left untouched, so muted taps
      // still contributed to feedback — a subtle semantic bug.)
      for (int i = 0; i < activeTaps; i++)
      {
        if (mTapEffectMode[i] == NETWORK_TAP_MUTE)
        {
          mTapGainL[i] = 0.0f;
          mTapGainR[i] = 0.0f;
          mFbWeight[i] = 0.0f;
        }
      }

      // ---- G8 — bitcrush + decimate (mode-gated, sub-classed) ----
      // CRUSH-mode taps get one of 3 sub-modes (equal probability
      // via independent hash):
      //   0: BITCRUSH_ONLY  — only bit reduction (gritty harmonic
      //                       distortion, no rate reduction)
      //   1: DECIMATE_ONLY  — only sample-rate reduction (clean
      //                       bit depth, aliased lo-fi steppy)
      //   2: BOTH           — combined crush (current character)
      // Sub-modes have categorically different artifact signatures
      // so the ear can separate them as distinct voices even with
      // many playing. Per-effect-applied param (bit depth or
      // decimate factor) is uniform [0,1] from independent
      // decoupled XOR-mask hashes. Identity values used for the
      // bypassed effect.
      // Larets ranges:
      //   bitParam ∈ [0,1] → bitLvl ∈ [4096 (12-bit), 5.66 (~2.5-bit)]
      //   decimParam ∈ [0,1] → factor ∈ [1, 32]
      // Identity bitLvl = 32768 (16-bit, finer than int16 input
      // → no audible quantization).
      for (int i = 0; i < activeTaps; i++)
      {
        if (mTapEffectMode[i] == NETWORK_TAP_CRUSH)
        {
          mTapCrushMask[i] = 1.0f;

          // Sub-mode hash (independent XOR mask).
          uint32_t hSub = mGlitchLcg ^
            ((uint32_t)i * 2654435761u + 0x55555555u);
          hSub = hSub * 1103515245u + 12345u;
          const uint32_t subSel = (hSub >> 16) % 3u;

          // Bit depth hash (used for BITCRUSH_ONLY and BOTH).
          uint32_t hBit = mGlitchLcg ^
            ((uint32_t)i * 2654435761u + 0xC3C3C3C3u);
          hBit = hBit * 1103515245u + 12345u;
          const float bitParam =
            (float)((hBit >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
          const float bitLvl = powf(2.0f, 12.0f - bitParam * 9.5f);

          // Decimate factor hash (used for DECIMATE_ONLY and BOTH).
          uint32_t hDecim = mGlitchLcg ^
            ((uint32_t)i * 2654435761u + 0x99CC55AAu);
          hDecim = hDecim * 1103515245u + 12345u;
          const float decimParam =
            (float)((hDecim >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
          const int factor = 1 + (int)(decimParam * 31.0f);

          // Identity values for bypassed effect.
          const float kIdentityBitLvl = 32768.0f;   // 16-bit, sub-input quantization

          if (subSel == 0u)
          {
            // BITCRUSH_ONLY
            mTapCrushBitLvl[i] = bitLvl;
            mTapCrushInvBitLvl[i] = 1.0f / bitLvl;
            mTapDecimFactorF[i] = 1.0f;
          }
          else if (subSel == 1u)
          {
            // DECIMATE_ONLY
            mTapCrushBitLvl[i] = kIdentityBitLvl;
            mTapCrushInvBitLvl[i] = 1.0f / kIdentityBitLvl;
            mTapDecimFactorF[i] = (float)factor;
          }
          else
          {
            // BOTH
            mTapCrushBitLvl[i] = bitLvl;
            mTapCrushInvBitLvl[i] = 1.0f / bitLvl;
            mTapDecimFactorF[i] = (float)factor;
          }
        }
        else
        {
          mTapCrushMask[i] = 0.0f;
          mTapCrushBitLvl[i] = 1.0f;
          mTapCrushInvBitLvl[i] = 1.0f;
          mTapDecimFactorF[i] = 1.0f;
        }
      }
      // Inactive taps — identity.
      for (int i = activeTaps; i < kMaxNetworkTaps; i++)
      {
        mTapCrushMask[i] = 0.0f;
        mTapCrushBitLvl[i] = 1.0f;
        mTapCrushInvBitLvl[i] = 1.0f;
        mTapDecimFactorF[i] = 1.0f;
      }

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
      // 50ms time constant for gain (pan-tracking, density-change
      // fade) and fb_weight (user-driven changes). Delay is no longer
      // smoothed here — replaced by dual-read crossfade pattern below.
      const float smoothAlpha = 1.0f / (0.05f * globalConfig.sampleRate);

      // ---- Block-rate dual-read shift ----
      // Doppler-free crossfading delay (ER-301 builtin Delay pattern,
      // mods/core/objects/delays/Delay.cpp:184). Each block: shift
      // mTapOldReadIdx[t] = mTapNewReadIdx[t] (carry over previous
      // block's "new" position), then compute fresh mTapNewReadIdx[t]
      // from current geometry-derived integer delay. Both indices
      // advance by 1 per sample within the block (no rate slewing →
      // no Doppler chirp). Pass C below crossfades from old read to
      // new read across the block via per-sample weight w (1 → 0).
      // Every block triggers a one-block-long fade. Fades chain
      // continuously, eliminating the singularity that produces
      // close-pass impulses.
      for (int t = 0; t < kMaxNetworkTaps; t++)
      {
        mTapOldReadIdx[t] = mTapNewReadIdx[t];   // carry over
        // Quantize current delay target to integer samples.
        int newDelay = (int)(mTapDelayTarget[t] + 0.5f);
        if (newDelay < 0) newDelay = 0;
        if (newDelay >= maxDelay) newDelay = maxDelay - 1;
        int idx = mWriteIndex - newDelay;
        if (idx < 0) idx += maxDelay;
        if (idx >= maxDelay) idx -= maxDelay;
        mTapNewReadIdx[t] = idx;
        // Snapshot geometry-derived idx before G5 scrub modifies it.
        // Graphic uses (mTapNewReadIdx - mTapGeomReadIdx) to recover
        // the SCRUB-mode offset for Z displacement encoding.
        mTapGeomReadIdx[t] = idx;
      }

      // ---- G5 scrub: per-block randomized read-pointer offset ----
      // SCRUB-mode taps get a random offset added to mTapNewReadIdx
      // each block. The dual-read crossfade smears each jump into
      // a 5ms morph — that morph is the scrub character. Offset
      // depth scales with size × glitch (per plan: scrubMax =
      // size × maxBufFrac × glitchAmount, maxBufFrac = 0.25).
      // Hash mixes in mWriteIndex/FRAMELENGTH (block counter) so
      // the offset re-randomizes every block, even though
      // mGlitchLcg is cycle-locked.
      const float kScrubMaxFrac = 0.25f;
      const int scrubMaxSamples =
        (int)(sizeNorm * kScrubMaxFrac * glitchAmount *
              (float)maxDelay + 0.5f);
      if (scrubMaxSamples > 0)
      {
        const uint32_t blockCounter =
          (uint32_t)(mWriteIndex / FRAMELENGTH);
        const int scrubSpan = 2 * scrubMaxSamples + 1;
        for (int t = 0; t < activeTaps; t++)
        {
          if (mTapEffectMode[t] != NETWORK_TAP_SCRUB) continue;
          uint32_t hScr = mGlitchLcg ^
            ((uint32_t)t * 2654435761u + 0x59B7C9F1u);
          hScr = hScr * 1103515245u + 12345u + blockCounter;
          hScr = hScr * 1103515245u + 12345u;
          const int offset =
            (int)((hScr >> 16) % (uint32_t)scrubSpan)
            - scrubMaxSamples;
          int idx = mTapNewReadIdx[t] + offset;
          while (idx < 0)         idx += maxDelay;
          while (idx >= maxDelay) idx -= maxDelay;
          mTapNewReadIdx[t] = idx;
        }
      }

      // ---- G6 reverse: align mNew with mOld for continuous reverse ----
      // Without this, the geometry-derived recompute (forward-
      // positioned) produces a 2*FRAMELENGTH discontinuity per
      // block on REVERSE taps. Setting mNew = mOld means both
      // indices stay in lockstep; per-sample Pass A advances both
      // by mTapReadAdvance (-1 for REVERSE), reading buffer
      // backwards continuously. Crossfade weight has no effect
      // (a == b in Pass C).
      for (int t = 0; t < activeTaps; t++)
      {
        if (mTapEffectMode[t] == NETWORK_TAP_REVERSE)
        {
          mTapNewReadIdx[t] = mTapOldReadIdx[t];
        }
      }

      // ---- G4 — input transient-triggered tap ricochet ----
      // Baseline behavior of the network — fires regardless of
      // glitch macro. When a transient is detected at the input
      // (envFast jumps above envSlow × threshold, past cooldown),
      // perturb K random taps with one of three sub-effects:
      // flip fb sign, duck gain ~50ms, or kick read pointer ±256
      // samples. K scales with connectivity × density (max 6 at
      // both full). At glitch=0 the effects ride over an all-
      // NORMAL field; at glitch>0 the mutex modes can absorb
      // some effects as no-ops (STUTTER taps especially) and
      // the surviving effects layer with the glitch character.
      {
        // Block peak input level.
        float blockMaxAbs = 0.0f;
        for (int i = 0; i < FRAMELENGTH; i++)
        {
          const float a = fabsf(in[i]);
          if (a > blockMaxAbs) blockMaxAbs = a;
        }
        // Fast env: short time-constant peak follower (~16ms).
        mEnvFast = (blockMaxAbs > mEnvFast * 0.7f)
                     ? blockMaxAbs : mEnvFast * 0.7f;
        // Slow env: long-time-constant moving average (~1s).
        mEnvSlow += (blockMaxAbs - mEnvSlow) * 0.005f;

        bool transientFired = false;
        if (mTransientCooldown > 0)
        {
          mTransientCooldown--;
        }
        else if (mEnvFast > mEnvSlow * 3.0f && mEnvFast > 0.05f)
        {
          transientFired = true;
          mTransientCooldown = 10;   // ~53ms refractory at 187 blocks/s
        }

        if (transientFired)
        {
          const int kMaxK = 6;
          // Baseline ricochet — independent of glitch. Both conn
          // and density gate intensity so high-conn-high-density
          // patches ricochet most strongly.
          const int K = (int)(connectivity * density *
                              (float)kMaxK + 0.5f);
          for (int n = 0; n < K; n++)
          {
            uint32_t h = mGlitchLcg ^
              ((uint32_t)n * 2654435761u + 0xE777E777u);
            h = h * 1103515245u + 12345u;
            const int t = (int)((h >> 16) % (uint32_t)activeTaps);
            // Skip G4 effects on STUTTER mode taps (no-op anyway,
            // wastes a slot).
            if (mTapEffectMode[t] == NETWORK_TAP_STUTTER) continue;
            // Light up the graphic ricochet flash for this tap.
            mTapRicochetFlash[t] = kRicochetFlashMax;
            h = h * 1103515245u + 12345u;
            const uint32_t effect = (h >> 16) % 3u;
            if (effect == 0u)
            {
              // Flip fb sign.
              mFbWeight[t] = -mFbWeight[t];
            }
            else if (effect == 1u)
            {
              // Duck gain — smoother snaps to 0, ramps back over
              // ~50ms via the per-sample smoother.
              mTapGainLSmoothed[t] = 0.0f;
              mTapGainRSmoothed[t] = 0.0f;
            }
            else
            {
              // Kick read pointer ±256 samples.
              h = h * 1103515245u + 12345u;
              const int offset = ((int)(h >> 16) & 0x1FF) - 0x100;
              int kickIdx = mTapNewReadIdx[t] + offset;
              while (kickIdx < 0)         kickIdx += maxDelay;
              while (kickIdx >= maxDelay) kickIdx -= maxDelay;
              mTapNewReadIdx[t] = kickIdx;
            }
          }
        }
      }

      // ---- G2 stutter — block-rate trigger / state ----
      // Mode-gated: only STUTTER-mode taps engage. Per-sample
      // playback (fractional reads with speed in {0.5, 1, 2}) runs
      // in a separate scalar pass after Pass C; here we just manage
      // the trigger and zero Pass C contributions for STUTTER taps
      // so the smoother ramps the lush body out at trigger.
      const int kStutterMinLenBlocks = 24;   // ~125ms (16th @ 120BPM)
      const int kStutterMaxLenBlocks = 96;   // ~512ms (quarter @ 120BPM)
      const int kStutterMinIterations = 2;
      const int kStutterMaxIterations = 8;
      const float kStutterGainBoost = 1.5f;
      for (int t = 0; t < kMaxNetworkTaps; t++)
      {
        if (mTapEffectMode[t] != NETWORK_TAP_STUTTER)
        {
          // Not in stutter mode — clear iter so any leftover state
          // doesn't keep playing into next cycle.
          mTapStutterIterations[t] = 0;
          continue;
        }

        if (mTapStutterIterations[t] > 0)
        {
          // Already stuttering — keep Pass C silenced; playback
          // handled per-sample below.
          mTapGainL[t] = 0.0f;
          mTapGainR[t] = 0.0f;
          mFbWeight[t] = 0.0f;
          continue;
        }

        // Trigger: capture gains BEFORE zeroing (so stutter pass
        // preserves L/R pan from geometry at trigger time).
        mTapStutterGainL[t] = mTapGainL[t] * kStutterGainBoost;
        mTapStutterGainR[t] = mTapGainR[t] * kStutterGainBoost;
        mTapStutterFbW[t]   = 0.0f;   // no self-feedback (avoids runaway in long stutters)

        // Now zero Pass C contributions for this stuttering tap.
        mTapGainL[t] = 0.0f;
        mTapGainR[t] = 0.0f;
        mFbWeight[t] = 0.0f;

        // Capture anchor at current geometry-derived read index.
        mTapStutterAnchor[t] = mTapNewReadIdx[t];

        uint32_t hStut = mGlitchLcg ^
          ((uint32_t)t * 2654435761u + 0x66666666u);
        hStut = hStut * 1103515245u + 12345u;

        // Loop length — triangular with mode=decay.
        const float lenU =
          (float)((hStut >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
        const float lenShaped = networkTriangularSample(lenU, decay);
        float baseLen = (float)kStutterMinLenBlocks +
          (float)(kStutterMaxLenBlocks - kStutterMinLenBlocks) * lenShaped;

        // Soft motion-cycle sync — halfway-snap to nearest cycle
        // subdivision in range.
        if (cyclePeriodBlocks > 0.0f)
        {
          const int kSubdivCount = 11;
          const int kSubdivs[] =
            { 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 };
          float bestSubdiv = baseLen;
          float bestDist = 1e9f;
          for (int s = 0; s < kSubdivCount; s++)
          {
            const float candidate =
              cyclePeriodBlocks / (float)kSubdivs[s];
            if (candidate >= (float)kStutterMinLenBlocks &&
                candidate <= (float)kStutterMaxLenBlocks)
            {
              const float d = fabsf(candidate - baseLen);
              if (d < bestDist) { bestDist = d; bestSubdiv = candidate; }
            }
          }
          const float kSnapStrength = 0.5f;
          baseLen = baseLen + kSnapStrength * (bestSubdiv - baseLen);
        }

        int lenBlocks = (int)(baseLen + 0.5f);
        if (lenBlocks < kStutterMinLenBlocks) lenBlocks = kStutterMinLenBlocks;
        if (lenBlocks > kStutterMaxLenBlocks) lenBlocks = kStutterMaxLenBlocks;
        mTapStutterLength[t] = (uint16_t)lenBlocks;

        // Zero-crossing alignment — shift both anchor and loop end
        // to nearest sign-change zero crossing within ±FRAME samples.
        // Sign-change is more rigorous than min-magnitude: it
        // guarantees the boundary is exactly at a zero crossing
        // (residual = 1 LSB int16), so wrap step is silent. Falls
        // back to min-magnitude when no sign change found in range.
        // Loop length shifts by at most ±10ms, imperceptible
        // against the musical-length budget.
        const int kZCSearchRange = (int)FRAMELENGTH;
        // Anchor refinement.
        mTapStutterAnchor[t] = networkFindNearestZeroCrossing(
          mTapStutterAnchor[t], kZCSearchRange, buf, maxDelay);
        // Loop-end refinement.
        const int baseLoopSamples = lenBlocks * (int)FRAMELENGTH;
        int targetEnd = mTapStutterAnchor[t] + baseLoopSamples;
        while (targetEnd >= maxDelay) targetEnd -= maxDelay;
        const int alignedEnd = networkFindNearestZeroCrossing(
          targetEnd, kZCSearchRange, buf, maxDelay);
        int adjusted = alignedEnd - mTapStutterAnchor[t];
        if (adjusted < 0) adjusted += maxDelay;
        mTapStutterLoopSamples[t] = (float)adjusted;

        // Iterations — triangular with mode=decay.
        hStut = hStut * 1103515245u + 12345u;
        const float iterU =
          (float)((hStut >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
        const float iterShaped = networkTriangularSample(iterU, decay);
        const int iterations = kStutterMinIterations +
          (int)((kStutterMaxIterations - kStutterMinIterations) * iterShaped + 0.5f);
        mTapStutterIterations[t] = (uint8_t)iterations;

        // Speed — uniform pick from {0.5, 1.0, 2.0}.
        hStut = hStut * 1103515245u + 12345u;
        const uint32_t speedSel = (hStut >> 16) % 3u;
        float speed = 1.0f;
        if (speedSel == 0u)      speed = 0.5f;
        else if (speedSel == 2u) speed = 2.0f;
        mTapStutterSpeed[t] = speed;

        // Initial playback state: start at anchor.
        mTapStutterPosInLoop[t] = 0.0f;
        mTapStutterReadPtr[t] = (float)mTapStutterAnchor[t];
      }

      // First-block snap: align oldRead with newRead so first block
      // doesn't crossfade from a stale (zero-init) position. Also
      // seed G1 S&H value so first activation has a sensible snapshot.
      if (mFirstProcess)
      {
        for (int t = 0; t < kMaxNetworkTaps; t++)
        {
          mTapOldReadIdx[t] = mTapNewReadIdx[t];
          mTapGainLSmoothed[t] = mTapGainL[t];
          mTapGainRSmoothed[t] = mTapGainR[t];
          mFbWeightSmoothed[t] = mFbWeight[t];
          mTapShValue[t] = mTapDelayTarget[t];
        }
        mFirstProcess = false;
      }

      // Crossfade weight ramp (1 → 0 across block). Precomputed once
      // since it's the same every block.
      const float kInvFrameLengthMinus1 = 1.0f / (float)(FRAMELENGTH - 1);

      // ---- Active-stutter list (built block-rate, capped) ----
      // Per-sample stutter pass iterates only this list. Hard cap
      // at kMaxActiveStutterTaps=16 — at density=1, glitch=1 the
      // STUTTER mode budget gives ~26 taps; capping to 16 ensures
      // a CPU ceiling. Excess STUTTER-mode taps stay silent
      // (mTapGainL/R + mFbWeight already zeroed by mode setup);
      // they just don't get scalar playback time.
      const int kMaxActiveStutterTaps = 16;
      int activeStutterTaps[kMaxActiveStutterTaps];
      int activeStutterCount = 0;
      for (int t = 0; t < activeTaps &&
                     activeStutterCount < kMaxActiveStutterTaps; t++)
      {
        if (mTapEffectMode[t] == NETWORK_TAP_STUTTER &&
            mTapStutterIterations[t] > 0)
        {
          activeStutterTaps[activeStutterCount++] = t;
        }
      }

      // ---- Pack active stutter state into class-member scratch ----
      // Per-sample stutter NEON pass operates on these contiguous
      // class-member arrays for clean 4-wide loads. Heap-allocated
      // → naturally aligned (no stack alignment risk per
      // feedback_neon_intrinsics_drumvoice). Read-only fields
      // (loopSamples, speed, anchor, gainL/R, fbW) gathered once
      // per block. Mutable fields (ptr, posInLoop, iter) scattered
      // back after the per-sample loop.
      for (int s = 0; s < activeStutterCount; s++)
      {
        const int t = activeStutterTaps[s];
        mStutPtr[s]         = mTapStutterReadPtr[t];
        mStutPosInLoop[s]   = mTapStutterPosInLoop[t];
        mStutLoopSamples[s] = mTapStutterLoopSamples[t];
        mStutSpeed[s]       = mTapStutterSpeed[t];
        mStutAnchor[s]      = mTapStutterAnchor[t];
        mStutGainL[s]       = mTapStutterGainL[t];
        mStutGainR[s]       = mTapStutterGainR[t];
        mStutFbW[s]         = mTapStutterFbW[t];
        mStutIter[s]        = mTapStutterIterations[t];
      }

      // ---- Scratch arrays for 3-pass tap processing ----
      int16_t sA[kMaxNetworkTaps];
      int16_t sB[kMaxNetworkTaps];
      const float scale = 1.0f / 32767.0f;

#ifdef NETWORK_HAS_NEON
      const float32x4_t alphaVec = vdupq_n_f32(smoothAlpha);
#endif

      // ---- Phase A cascade path ----
      // When kUseCascade is true (compile-time const), the serial
      // cascade runs and we early-return from process(). The
      // legacy star-multitap per-sample loop below is dead code
      // in that build configuration. Set kUseCascade=false to
      // rebuild with the original star-multitap engine for
      // regression / A-B comparison. See
      // planning/network-cascade-rebuild-plan.md.
      static const bool kUseCascade = true;
      if (kUseCascade)
      {
        // Overwrite mTapDelayTarget with cascade group-relative
        // values. Discards per-tap LFO / S1 detune / S&H modifications
        // applied above — Phase A audits a clean cascade structure.
        network_geom::recomputeCascadeTaps(
          kMaxNetworkTaps,
          activeTaps,
          kNetworkNumGroupsMax,
          kNetworkGroupSize,
          sizeNorm,
          mGroupLen,
          mTapGroupMap,
          mTapIntraGroupOffset,
          mTapDelayTarget);

        // Compute initial mTapNewReadIdx (group-relative) for each
        // active tap from its owning group's current write index
        // minus the tap's delay. Per-sample loop advances both by
        // 1 in lockstep so the relative offset (the delay) stays
        // constant within a block. Also accumulate average delay
        // per group for the Jot attenuation coefficient.
        float groupAvgDelay[kNetworkNumGroupsMax];
        for (int g = 0; g < kNetworkNumGroupsMax; g++) groupAvgDelay[g] = 0.0f;
        for (int g = 0; g < kNetworkNumGroupsMax; g++)
        {
          const int gLen = mGroupLen[g];
          if (gLen <= 0) continue;
          const int tBase = g * kNetworkGroupSize;
          float dlySum = 0.0f;
          for (int k = 0; k < kNetworkGroupSize; k++)
          {
            const int t = tBase + k;
            int dly = (int)(mTapDelayTarget[t] + 0.5f);
            if (dly < 0) dly = 0;
            if (dly >= gLen) dly = gLen - 1;
            int idx_g = mGroupWriteIndex[g] - dly;
            while (idx_g < 0)     idx_g += gLen;
            while (idx_g >= gLen) idx_g -= gLen;
            mTapNewReadIdx[t] = idx_g;
            dlySum += (float)dly;
          }
          groupAvgDelay[g] = dlySum * (1.0f / (float)kNetworkGroupSize);
        }

        // Jot per-group attenuation. Map decay knob to T60 in
        // seconds: T60 = 0.05 + decay² × 5 → 50 ms at decay=0
        // (essentially no reverb tail beyond the cascade itself),
        // 5.05 s at decay=1. Quadratic curve gives finer control
        // at the short end where most musical use lives.
        //
        // Per-group coefficient: 10^(-3 × D_g / (T60 × Fs)). For a
        // delay line of D_g samples, this is the per-round-trip
        // gain that produces a -60 dB decay in T60 seconds. Floor
        // at 0 to avoid denormals; clamp to <1 for stability.
        // Coefficients computed block-rate (cheap) and applied
        // per-sample as a single multiply on each group's
        // Hadamard-mixed feedback.
        {
          const float kT60Floor = 0.05f;
          const float kT60Span  = 5.0f;
          const float T60 = kT60Floor + decay * decay * kT60Span;
          const float Fs = globalConfig.sampleRate;
          const float coefExpScale =
            -3.0f * 2.302585f / (T60 * Fs);   // -3 × ln(10) / (T60 × Fs)
          for (int g = 0; g < kNetworkNumGroupsMax; g++)
          {
            const float D_g = groupAvgDelay[g];
            // expf is fine block-rate. clamp to (0, 0.999) — 0.999 cap
            // keeps spectral radius < 1 even at decay=1 / very short
            // delays so the FDN never goes lossless.
            float c = (D_g > 0.0f) ? expf(coefExpScale * D_g) : 0.0f;
            if (c > 0.999f) c = 0.999f;
            mGroupDecayCoef[g] = c;
          }
        }

        // Number of active cascade groups.
        int activeGroupsLocal =
          (activeTaps + kNetworkGroupSize - 1) / kNetworkGroupSize;
        if (activeGroupsLocal < 1) activeGroupsLocal = 1;
        if (activeGroupsLocal > kNetworkNumGroupsMax)
          activeGroupsLocal = kNetworkNumGroupsMax;
        const int aG = activeGroupsLocal;

        const float scale = 1.0f / 32767.0f;

#ifdef NETWORK_HAS_NEON
        const float32x4_t alphaVecCsc = vdupq_n_f32(smoothAlpha);
#endif

        // Per-sample cascade loop. Phase A scaffolding: feedback
        // OFF, no per-tap LP/crush, no stutter playback. Just
        // input → group 0 → group 1 → ... → wet bus.
        for (int i = 0; i < FRAMELENGTH; i++)
        {
          // Input DC blocker
          const float xRaw = in[i] * inputLevel;
          const float x = xRaw - mDcInX1 + kNetworkDcR * mDcInY1;
          mDcInX1 = xRaw;
          mDcInY1 = x;

          // Per-sample gain smoother (NEON 4-wide).
#ifdef NETWORK_HAS_NEON
          {
            int t = 0;
            for (; t + 4 <= kMaxNetworkTaps; t += 4)
            {
              float32x4_t tgt = vld1q_f32(&mTapGainL[t]);
              float32x4_t sm  = vld1q_f32(&mTapGainLSmoothed[t]);
              sm = vmlaq_f32(sm, vsubq_f32(tgt, sm), alphaVecCsc);
              vst1q_f32(&mTapGainLSmoothed[t], sm);

              tgt = vld1q_f32(&mTapGainR[t]);
              sm  = vld1q_f32(&mTapGainRSmoothed[t]);
              sm = vmlaq_f32(sm, vsubq_f32(tgt, sm), alphaVecCsc);
              vst1q_f32(&mTapGainRSmoothed[t], sm);
            }
          }
#else
          for (int t = 0; t < kMaxNetworkTaps; t++)
          {
            mTapGainLSmoothed[t] +=
              (mTapGainL[t] - mTapGainLSmoothed[t]) * smoothAlpha;
            mTapGainRSmoothed[t] +=
              (mTapGainR[t] - mTapGainRSmoothed[t]) * smoothAlpha;
          }
#endif

          // Cascade per-group serial loop.
          float g_prev_out = 0.0f;
          float wetL = 0.0f;
          float wetR = 0.0f;

          // Local feedback gain, decay-coupled with full perceptual
          // range: at decay=0 → 0.1×conn (minimal per-group ring,
          // cascade reads as discrete echo chain); at decay=1 →
          // 0.8×conn (strong per-group ringing tails). Stays
          // sub-unity so bufWrite tanh + per-group LP+HPF band-pass
          // keep the loop stable. Wider range than the prior
          // 0.35-0.75 (which felt like "always somewhat ringing")
          // gives decay a clear on/off perceptual axis on the
          // short-time character.
          const float kLocalFbScale =
            connectivity * (0.1f + 0.7f * decay);

          // Hadamard FDN cross-feed scale. The 16×16 Hadamard matrix
          // with entries ±1 has operator norm √16; dividing by √16=4
          // gives a unitary matrix with operator norm 1. The actual
          // T60 control comes from mGroupDecayCoef[g] (Jot per-line
          // attenuation, computed block-rate above); kCrossFeedScale
          // here is only the matrix normalization × connectivity
          // (which gates "is the FDN cross-feed active at all").
          // Per Stautner-Puckette / Jot, the Hadamard matrix is the
          // canonical choice for maximum inter-channel mixing with N
          // orthogonal modes; per Jot 1991, T60 control in an FDN
          // must come from per-delay-line gains scaled to each line's
          // length — uniform matrix gain gives wrong T60 because
          // short and long delays go around the loop at different
          // rates per second.
          const float kHadamardNorm = 0.25f;  // 1/√16
          const float kCrossFeedScale = connectivity * kHadamardNorm;

          // Local feedback LP coefficient (one-pole). Couples to
          // decay so short delays at high decay get more damping
          // (preventing Karplus-Strong-style resonant runaway in
          // tight per-group loops). Cutoff range: ~5 kHz @ decay=0
          // (alpha=0.5, light damping) → ~1 kHz @ decay=1
          // (alpha=0.15, heavy damping). The damping is what
          // makes per-group loops decay naturally instead of
          // self-oscillating at the delay-length fundamental.
          const float kFbLpAlpha = 0.5f - 0.35f * decay;

          // ---- Hadamard FDN cross-feed computation ----
          // Compute M = H_16 × G where G is the previous-sample
          // per-group output vector (mGroupLocalFbState). Uses
          // Fast Walsh-Hadamard Transform (FWHT) butterflies — 4
          // stages × 8 add/sub butterflies = 64 ops, no multiplies.
          // Result M[g] is each group's cross-feed contribution from
          // all 15 other groups with orthogonal ±1 weighting.
          //
          // Decay=0 → kCrossFeedScale=0 → no cross-feed → cascade
          // behaves as 16 independent damped delay lines.
          // Decay=1 → marginal stability → infinite-tail reverb.
          for (int g = 0; g < kNetworkNumGroupsMax; g++)
            mHadamardScratch[g] = mGroupLocalFbState[g];
          for (int len = 1; len < kNetworkNumGroupsMax; len <<= 1)
          {
            for (int s = 0; s < kNetworkNumGroupsMax; s += (len << 1))
            {
              for (int j = s; j < s + len; j++)
              {
                const float a = mHadamardScratch[j];
                const float b = mHadamardScratch[j + len];
                mHadamardScratch[j]       = a + b;
                mHadamardScratch[j + len] = a - b;
              }
            }
          }

          for (int g = 0; g < aG; g++)
          {
            const int gOrig = mGroupOrigin[g];
            const int gLen  = mGroupLen[g];
            if (gLen <= 0) continue;

            // Group input = cascade-flow + local feedback (and
            // diffused global pool for group 0 only). All three
            // sum before the bufWrite tanh.
            //
            // Local feedback path passes through a one-pole LP
            // filter (kFbLpAlpha, decay-coupled) BEFORE injection.
            // This is what makes the per-group loop a damped
            // resonator rather than an undamped Karplus-Strong
            // string. Without it, short per-group delays
            // (e.g., size=0.35 → ~140 Hz fundamental) self-
            // oscillate at high conn, producing audible rumble.
            const float fbRaw = mGroupLocalFbState[g];
            mGroupFbLpState[g] +=
              (fbRaw - mGroupFbLpState[g]) * kFbLpAlpha;
            const float fbLp = mGroupFbLpState[g];

            // HPF on the LP'd signal → band-pass on the local fb.
            // Catches sub-150 Hz buildup that the LP otherwise
            // accumulates at short loop lengths (low size → loop
            // fundamental falls inside LP cutoff). R = 0.987 →
            // ~100 Hz cutoff at 48 kHz, same shape as the pool HPF.
            const float kFbHpR = 0.987f;
            const float fbDamped =
              fbLp - mGroupFbHpX1[g] + kFbHpR * mGroupFbHpY1[g];
            mGroupFbHpX1[g] = fbLp;
            mGroupFbHpY1[g] = fbDamped;

            const float group_in_cascade = (g == 0) ? x : g_prev_out;
            float group_in = group_in_cascade +
              fbDamped * kLocalFbScale +
              mHadamardScratch[g] * kCrossFeedScale * mGroupDecayCoef[g];

            // Write tanh(group_in) to group's sub-window head.
            bufWrite(buf,
                     gOrig + mGroupWriteIndex[g],
                     networkFastTanh(group_in));

            // Per-tap reads (4 taps per group).
            float groupMono = 0.0f;
            float groupWetL = 0.0f;
            float groupWetR = 0.0f;
            const int tBase = g * kNetworkGroupSize;
            const int kLim =
              ((tBase + kNetworkGroupSize) <= activeTaps)
                ? kNetworkGroupSize
                : (activeTaps - tBase);
            for (int k = 0; k < kLim; k++)
            {
              const int t = tBase + k;
              // Advance read pointer (mod groupLen).
              int idx = mTapNewReadIdx[t] + 1;
              if (idx >= gLen) idx = 0;
              mTapNewReadIdx[t] = idx;
              const float s = (float)buf[gOrig + idx] * scale;
              groupMono += s;
              groupWetL += s * mTapGainLSmoothed[t];
              groupWetR += s * mTapGainRSmoothed[t];
            }

            wetL += groupWetL;
            wetR += groupWetR;

            // Advance group write index.
            int gw = mGroupWriteIndex[g] + 1;
            if (gw >= gLen) gw = 0;
            mGroupWriteIndex[g] = gw;

            // Cascade flow normalization: 1/kNetworkGroupSize
            // makes groupMono the AVERAGE of the 4 tap reads
            // rather than their sum. Prevents inter-stage gain
            // runaway (without this, each group's bufWrite
            // tanh-saturates the prior stage's amplified signal,
            // producing square-wave harshness by group 5-10).
            // Precedent: legacy code uses densityCompGain for wet
            // bus tap energy and 1/sqrt(k) for feedback summing.
            // The cascade adds a third normalization scope —
            // inter-stage — that the original star multitap didn't
            // need because there were no stages. Wet bus left
            // un-normalized for Phase A (matches legacy summation
            // pattern at equivalent density); revisit at Phase B
            // when feedback enters and changes wet character.
            const float kCascadeStageNorm =
              1.0f / (float)kNetworkGroupSize;
            g_prev_out = groupMono * kCascadeStageNorm;

            // Per-group one-pole DC blocker. Catches DC drift
            // from asymmetric tanh saturation in the local
            // recirculation before it amplifies through subsequent
            // samples. Applied here so all three downstream
            // consumers (next group's cascade input, local fb
            // state, pool contribution) see the cleaned signal.
            {
              const float dcX = mGroupDcX1[g];
              const float dcY = mGroupDcY1[g];
              const float dcOut =
                g_prev_out - dcX + kNetworkDcR * dcY;
              mGroupDcX1[g] = g_prev_out;
              mGroupDcY1[g] = dcOut;
              g_prev_out = dcOut;
            }

            // Local feedback state for NEXT sample. Stored as the
            // DC-blocked, normalized cascade output. Also forms the
            // input vector for next sample's Hadamard cross-feed.
            mGroupLocalFbState[g] = g_prev_out;
          }
          // Pool path removed: replaced by the Hadamard FDN cross-feed
          // computed at the top of the sample. Hadamard provides
          // full-rank mode mixing where the pool was rank-1.
          // mDcFbX1/Y1, mGroupPoolSign, mPoolHpX1/Y1, mDiffusedGlobalPool,
          // mApBuf{1..4} retained as declared but unused in this path.

          // Wet bus compensation. The cascade signal flow itself
          // is correctly normalized (1/N per stage prevents
          // gain runaway), but EACH stage's bufWrite applies
          // tanh, which geometrically attenuates the signal that
          // sits in the buffers — tanh(1.0)≈0.76, tanh(0.76)≈0.64,
          // etc. After ~16 stages of cascade-cumulative tanh, the
          // buffer contents are at ~0.5x of the input level on
          // average across groups. Wet bus reads sample those
          // attenuated buffer regions, so the summed wet output
          // lands at ~0.5x of input at full wet. This compensation
          // restores ~unity wet output at full wet, decoupled
          // from cascade-flow stability. Empirically calibrated
          // from 2.6.1.65 audition (user reported 0.5x wet at
          // density=1 / aG=16). Coupled to active group count so
          // shorter cascades (fewer stages of attenuation) get
          // proportionally less boost.
          //
          // Compensation = 1.0 + (aG/16) — at aG=1, no boost
          // (single stage, minimal cumulative atten); at aG=16,
          // 2x boost matches measured 0.5x undervolume.
          const float kCascadeWetComp =
            1.0f + (float)aG * (1.0f / 16.0f);
          wetL *= kCascadeWetComp;
          wetR *= kCascadeWetComp;

          // Mix.
          const float mixedL = x * (1.0f - wet) + wetL * wet;
          const float mixedR = x * (1.0f - wet) + wetR * wet;

          // Output DC blockers.
          const float outDcL =
            mixedL - mDcOutLX1 + kNetworkDcR * mDcOutLY1;
          mDcOutLX1 = mixedL;
          mDcOutLY1 = outDcL;
          const float outDcR =
            mixedR - mDcOutRX1 + kNetworkDcR * mDcOutRY1;
          mDcOutRX1 = mixedR;
          mDcOutRY1 = outDcR;

          outL[i] = outDcL;
          outR[i] = outDcR;

          // Viz output ring.
          mOutputRing[mOutputRingPos] = 0.5f * (outDcL + outDcR);
          mOutputRingPos =
            (mOutputRingPos + 1) & (kOutputRingSize - 1);
        }

        // Block-rate listener trace write.
        mListenerTrace[mListenerTraceHead] = mWalkerPos;
        mListenerTraceHead =
          (mListenerTraceHead + 1) & (kListenerTraceSize - 1);

        return;
      }

      // ---- Per-sample inner loop (legacy star-multitap) ----
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        // Input DC blocker: y[n] = x[n] - x[n-1] + R*y[n-1]. Removes
        // any DC the user might patch in; protects feedback loop.
        const float xRaw = in[i] * inputLevel;
        const float x = xRaw - mDcInX1 + kNetworkDcR * mDcInY1;
        mDcInX1 = xRaw;
        mDcInY1 = x;

        if (mWriteIndex >= maxDelay) mWriteIndex = 0;

        // Crossfade weight: 1 at i=0, 0 at i=FRAMELENGTH-1. Per-sample
        // scalar, broadcast in NEON Pass C.
        const float w = 1.0f - (float)i * kInvFrameLengthMinus1;

        // ---- Per-sample gain/fb_weight smoother step ----
        // One-pole LP on per-tap gain and fb_weight targets. Iterates
        // over kMaxNetworkTaps so taps fading from inactive→active
        // (density changes) get smooth gain ramps. Delay smoothing
        // is gone — replaced by dual-read crossfade below.
#ifdef NETWORK_HAS_NEON
        {
          int t = 0;
          for (; t + 4 <= kMaxNetworkTaps; t += 4)
          {
            // Gain L
            float32x4_t tgt = vld1q_f32(&mTapGainL[t]);
            float32x4_t sm  = vld1q_f32(&mTapGainLSmoothed[t]);
            sm = vmlaq_f32(sm, vsubq_f32(tgt, sm), alphaVec);
            vst1q_f32(&mTapGainLSmoothed[t], sm);

            // Gain R
            tgt = vld1q_f32(&mTapGainR[t]);
            sm  = vld1q_f32(&mTapGainRSmoothed[t]);
            sm = vmlaq_f32(sm, vsubq_f32(tgt, sm), alphaVec);
            vst1q_f32(&mTapGainRSmoothed[t], sm);

            // Feedback weight
            tgt = vld1q_f32(&mFbWeight[t]);
            sm  = vld1q_f32(&mFbWeightSmoothed[t]);
            sm = vmlaq_f32(sm, vsubq_f32(tgt, sm), alphaVec);
            vst1q_f32(&mFbWeightSmoothed[t], sm);
          }
        }
#else
        for (int t = 0; t < kMaxNetworkTaps; t++)
        {
          mTapGainLSmoothed[t] += (mTapGainL[t] - mTapGainLSmoothed[t]) * smoothAlpha;
          mTapGainRSmoothed[t] += (mTapGainR[t] - mTapGainRSmoothed[t]) * smoothAlpha;
          mFbWeightSmoothed[t] += (mFbWeight[t] - mFbWeightSmoothed[t]) * smoothAlpha;
        }
#endif

        // ---- Pass A (dual-read advance) ----
        // Per-tap signed advance from mTapReadAdvance[t]: +1 for
        // forward (default) or -1 for REVERSE-mode taps. Wrap is
        // bidirectional: idx >= maxDelay → idx -= maxDelay,
        // idx < 0 → idx += maxDelay. Branchless via vbslq_s32.
#ifdef NETWORK_HAS_NEON
        {
          const int32x4_t maxDelayVec = vdupq_n_s32(maxDelay);
          const int32x4_t zeroIVec = vdupq_n_s32(0);

          int t = 0;
          for (; t + 4 <= activeTaps; t += 4)
          {
            int32x4_t advance = vld1q_s32(&mTapReadAdvance[t]);

            // Old read
            int32x4_t oldIdx = vld1q_s32(&mTapOldReadIdx[t]);
            oldIdx = vaddq_s32(oldIdx, advance);
            uint32x4_t hiO = vcgeq_s32(oldIdx, maxDelayVec);
            oldIdx = vbslq_s32(hiO, vsubq_s32(oldIdx, maxDelayVec), oldIdx);
            uint32x4_t loO = vcltq_s32(oldIdx, zeroIVec);
            oldIdx = vbslq_s32(loO, vaddq_s32(oldIdx, maxDelayVec), oldIdx);
            vst1q_s32(&mTapOldReadIdx[t], oldIdx);

            // New read
            int32x4_t newIdx = vld1q_s32(&mTapNewReadIdx[t]);
            newIdx = vaddq_s32(newIdx, advance);
            uint32x4_t hiN = vcgeq_s32(newIdx, maxDelayVec);
            newIdx = vbslq_s32(hiN, vsubq_s32(newIdx, maxDelayVec), newIdx);
            uint32x4_t loN = vcltq_s32(newIdx, zeroIVec);
            newIdx = vbslq_s32(loN, vaddq_s32(newIdx, maxDelayVec), newIdx);
            vst1q_s32(&mTapNewReadIdx[t], newIdx);
          }
          for (; t < activeTaps; t++)
          {
            const int adv = mTapReadAdvance[t];
            int o = mTapOldReadIdx[t] + adv;
            if (o >= maxDelay) o -= maxDelay;
            if (o < 0)         o += maxDelay;
            mTapOldReadIdx[t] = o;
            int n = mTapNewReadIdx[t] + adv;
            if (n >= maxDelay) n -= maxDelay;
            if (n < 0)         n += maxDelay;
            mTapNewReadIdx[t] = n;
          }
        }
#else
        for (int t = 0; t < activeTaps; t++)
        {
          const int adv = mTapReadAdvance[t];
          int o = mTapOldReadIdx[t] + adv;
          if (o >= maxDelay) o -= maxDelay;
          if (o < 0)         o += maxDelay;
          mTapOldReadIdx[t] = o;
          int n = mTapNewReadIdx[t] + adv;
          if (n >= maxDelay) n -= maxDelay;
          if (n < 0)         n += maxDelay;
          mTapNewReadIdx[t] = n;
        }
#endif

        // ---- Pass B (scalar gather + 8-ahead prefetch on both reads) ----
        // sA from old read pointer, sB from new read pointer. Both
        // reads come from random-access regions of the delay buffer
        // (per-tap delays); prefetching ahead on both halves the
        // L1-miss exposure during the scalar gather.
        for (int t = 0; t < activeTaps; t++)
        {
          int pfIdx = t + 8;
          if (pfIdx < activeTaps)
          {
            __builtin_prefetch(&buf[mTapOldReadIdx[pfIdx]], 0, 1);
            __builtin_prefetch(&buf[mTapNewReadIdx[pfIdx]], 0, 1);
          }
          sA[t] = buf[mTapOldReadIdx[t]];
          sB[t] = buf[mTapNewReadIdx[t]];
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
          // Crossfade weight: per-sample scalar w in [0, 1] broadcast
          // across all 4-tap iterations. tapV = bV + (aV - bV) * w.
          // At i=0, w=1 → all sA. At i=FRAMELENGTH-1, w=0 → all sB.
          // Smooth blend across block from old read to new read.
          const float32x4_t wVec = vdupq_n_f32(w);
          int t = 0;
          for (; t + 4 <= activeTaps; t += 4)
          {
            int16x4_t sAi = vld1_s16(&sA[t]);
            int16x4_t sBi = vld1_s16(&sB[t]);
            float32x4_t aV = vmulq_f32(vcvtq_f32_s32(vmovl_s16(sAi)), scaleVec);
            float32x4_t bV = vmulq_f32(vcvtq_f32_s32(vmovl_s16(sBi)), scaleVec);
            float32x4_t gLV = vld1q_f32(&mTapGainLSmoothed[t]);
            float32x4_t gRV = vld1q_f32(&mTapGainRSmoothed[t]);
            float32x4_t fbWV = vld1q_f32(&mFbWeightSmoothed[t]);
            // Crossfade old → new read via shared per-sample weight.
            float32x4_t tapV = vmlaq_f32(bV, vsubq_f32(aV, bV), wVec);
            // L3: per-tap one-pole LP. state += coeff * (tap - state);
            // tap = state. Five ops per lane.
            float32x4_t lpState = vld1q_f32(&mTapLpState[t]);
            float32x4_t lpCoeff = vld1q_f32(&mTapLpCoeff[t]);
            lpState = vmlaq_f32(lpState, vsubq_f32(tapV, lpState), lpCoeff);
            vst1q_f32(&mTapLpState[t], lpState);
            tapV = lpState;

            // G8: branchless bitcrush + decimate. Every tap pays the
            // work; mTapCrushMask blends crushed vs original (un-
            // crushed taps have identity bitLvl=1, factor=1, so the
            // computed crushed value equals tapV anyway, and mask=0
            // gates it out). Decimate state advances regardless.
            float32x4_t cnt  = vld1q_f32(&mTapDecimCounterF[t]);
            float32x4_t fct  = vld1q_f32(&mTapDecimFactorF[t]);
            cnt = vaddq_f32(cnt, vdupq_n_f32(1.0f));
            uint32x4_t wrapM = vcgeq_f32(cnt, fct);
            float32x4_t held = vld1q_f32(&mTapDecimHold[t]);
            held = vbslq_f32(wrapM, tapV, held);
            cnt  = vbslq_f32(wrapM, vdupq_n_f32(0.0f), cnt);
            vst1q_f32(&mTapDecimHold[t], held);
            vst1q_f32(&mTapDecimCounterF[t], cnt);
            // Bitcrush via integer truncation (NEON Cortex-A8 has no
            // round-to-nearest; trunc-toward-zero gives slight
            // asymmetry that the output DC blocker absorbs).
            float32x4_t bLvl   = vld1q_f32(&mTapCrushBitLvl[t]);
            float32x4_t invBL  = vld1q_f32(&mTapCrushInvBitLvl[t]);
            float32x4_t scaled = vmulq_f32(held, bLvl);
            int32x4_t   truncI = vcvtq_s32_f32(scaled);
            float32x4_t crushed = vmulq_f32(vcvtq_f32_s32(truncI), invBL);
            // Blend: tapV += crushMask × (crushed - tapV)
            float32x4_t crM = vld1q_f32(&mTapCrushMask[t]);
            tapV = vmlaq_f32(tapV, vsubq_f32(crushed, tapV), crM);

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
            float tapOut = b + (a - b) * w;
            // L3 per-tap LP.
            mTapLpState[t] += mTapLpCoeff[t] * (tapOut - mTapLpState[t]);
            tapOut = mTapLpState[t];
            // G8 bitcrush + decimate (Larets formula in scalar path).
            mTapDecimCounterF[t] += 1.0f;
            if (mTapDecimCounterF[t] >= mTapDecimFactorF[t])
            {
              mTapDecimHold[t] = tapOut;
              mTapDecimCounterF[t] = 0.0f;
            }
            const float crushedS =
              floorf(mTapDecimHold[t] * mTapCrushBitLvl[t] + 0.5f)
              * mTapCrushInvBitLvl[t];
            tapOut += mTapCrushMask[t] * (crushedS - tapOut);
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
          float tapOut = b + (a - b) * w;
          // L3 per-tap LP.
          mTapLpState[t] += mTapLpCoeff[t] * (tapOut - mTapLpState[t]);
          tapOut = mTapLpState[t];
          // G8 bitcrush + decimate (Larets formula).
          mTapDecimCounterF[t] += 1.0f;
          if (mTapDecimCounterF[t] >= mTapDecimFactorF[t])
          {
            mTapDecimHold[t] = tapOut;
            mTapDecimCounterF[t] = 0.0f;
          }
          const float crushedS =
            floorf(mTapDecimHold[t] * mTapCrushBitLvl[t] + 0.5f)
            * mTapCrushInvBitLvl[t];
          tapOut += mTapCrushMask[t] * (crushedS - tapOut);
          wetL += tapOut * mTapGainLSmoothed[t];
          wetR += tapOut * mTapGainRSmoothed[t];
          fbSum += tapOut * mFbWeightSmoothed[t];
        }
#endif

        // ---- Stutter playback pass (NEON 4-wide + scalar tail) ----
        // Operates on class-member packed scratch arrays gathered at
        // block start. Per 4-tap NEON iteration: branchless ptr wrap,
        // vcvt to int + frac, scalar gather buf samples via lane
        // intrinsics (no stack store), NEON linear interp, NEON FMA
        // accumulate into wetL/R/fb vector accumulators. Per-lane
        // wrap detection drives scalar re-anchor + iter decrement.
        // Tail loop handles activeStutterCount % 4.
#ifdef NETWORK_HAS_NEON
        {
          float32x4_t wetLAcc = vdupq_n_f32(0.0f);
          float32x4_t wetRAcc = vdupq_n_f32(0.0f);
          float32x4_t fbAcc   = vdupq_n_f32(0.0f);
          const float32x4_t maxDV  = vdupq_n_f32((float)maxDelay);
          const float32x4_t scaleV = vdupq_n_f32(scale);

          int s = 0;
          for (; s + 4 <= activeStutterCount; s += 4)
          {
            // Load + wrap ptr (branchless single-subtract via mask).
            float32x4_t ptrV = vld1q_f32(&mStutPtr[s]);
            uint32x4_t hiPtr = vcgeq_f32(ptrV, maxDV);
            ptrV = vbslq_f32(hiPtr, vsubq_f32(ptrV, maxDV), ptrV);

            // iptr (truncation) and frac.
            int32x4_t iptrV  = vcvtq_s32_f32(ptrV);
            float32x4_t fracV = vsubq_f32(ptrV, vcvtq_f32_s32(iptrV));

            // Per-lane scalar gather via vgetq_lane_s32 (no stack
            // store intermediate to avoid :64/:128 alignment hints).
            const int i0_0 = vgetq_lane_s32(iptrV, 0);
            const int i0_1 = vgetq_lane_s32(iptrV, 1);
            const int i0_2 = vgetq_lane_s32(iptrV, 2);
            const int i0_3 = vgetq_lane_s32(iptrV, 3);
            int i1_0 = i0_0 + 1; if (i1_0 >= maxDelay) i1_0 -= maxDelay;
            int i1_1 = i0_1 + 1; if (i1_1 >= maxDelay) i1_1 -= maxDelay;
            int i1_2 = i0_2 + 1; if (i1_2 >= maxDelay) i1_2 -= maxDelay;
            int i1_3 = i0_3 + 1; if (i1_3 >= maxDelay) i1_3 -= maxDelay;

            float32x4_t aV = vdupq_n_f32(0.0f);
            aV = vsetq_lane_f32((float)buf[i0_0], aV, 0);
            aV = vsetq_lane_f32((float)buf[i0_1], aV, 1);
            aV = vsetq_lane_f32((float)buf[i0_2], aV, 2);
            aV = vsetq_lane_f32((float)buf[i0_3], aV, 3);
            float32x4_t bV = vdupq_n_f32(0.0f);
            bV = vsetq_lane_f32((float)buf[i1_0], bV, 0);
            bV = vsetq_lane_f32((float)buf[i1_1], bV, 1);
            bV = vsetq_lane_f32((float)buf[i1_2], bV, 2);
            bV = vsetq_lane_f32((float)buf[i1_3], bV, 3);
            aV = vmulq_f32(aV, scaleV);
            bV = vmulq_f32(bV, scaleV);
            // Linear interp: sample = a + (b - a) * frac.
            float32x4_t sampleV = vmlaq_f32(aV,
                                            vsubq_f32(bV, aV), fracV);

            // Triple FMA into vector accumulators.
            wetLAcc = vmlaq_f32(wetLAcc, sampleV,
                                vld1q_f32(&mStutGainL[s]));
            wetRAcc = vmlaq_f32(wetRAcc, sampleV,
                                vld1q_f32(&mStutGainR[s]));
            fbAcc   = vmlaq_f32(fbAcc,   sampleV,
                                vld1q_f32(&mStutFbW[s]));

            // Advance ptr and posInLoop.
            const float32x4_t spdV = vld1q_f32(&mStutSpeed[s]);
            ptrV = vaddq_f32(ptrV, spdV);
            vst1q_f32(&mStutPtr[s], ptrV);

            float32x4_t posV = vld1q_f32(&mStutPosInLoop[s]);
            posV = vaddq_f32(posV, spdV);

            // Loop wrap (per-lane): on wrap, subtract loopSamples.
            const float32x4_t loopV = vld1q_f32(&mStutLoopSamples[s]);
            uint32x4_t wrapMask = vcgeq_f32(posV, loopV);
            posV = vbslq_f32(wrapMask,
                             vsubq_f32(posV, loopV), posV);
            vst1q_f32(&mStutPosInLoop[s], posV);

            // Per-lane wrap handling via vgetq_lane_u32 (no stack
            // store): re-anchor ptr and decrement iter on wrapped
            // lanes. Scalar since iter is uint8.
            const uint32_t w0 = vgetq_lane_u32(wrapMask, 0);
            const uint32_t w1 = vgetq_lane_u32(wrapMask, 1);
            const uint32_t w2 = vgetq_lane_u32(wrapMask, 2);
            const uint32_t w3 = vgetq_lane_u32(wrapMask, 3);
            if (w0)
            {
              float reanchored =
                (float)mStutAnchor[s] + mStutPosInLoop[s];
              if (reanchored >= (float)maxDelay)
                reanchored -= (float)maxDelay;
              mStutPtr[s] = reanchored;
              if (mStutIter[s] > 0) mStutIter[s]--;
            }
            if (w1)
            {
              float reanchored =
                (float)mStutAnchor[s+1] + mStutPosInLoop[s+1];
              if (reanchored >= (float)maxDelay)
                reanchored -= (float)maxDelay;
              mStutPtr[s+1] = reanchored;
              if (mStutIter[s+1] > 0) mStutIter[s+1]--;
            }
            if (w2)
            {
              float reanchored =
                (float)mStutAnchor[s+2] + mStutPosInLoop[s+2];
              if (reanchored >= (float)maxDelay)
                reanchored -= (float)maxDelay;
              mStutPtr[s+2] = reanchored;
              if (mStutIter[s+2] > 0) mStutIter[s+2]--;
            }
            if (w3)
            {
              float reanchored =
                (float)mStutAnchor[s+3] + mStutPosInLoop[s+3];
              if (reanchored >= (float)maxDelay)
                reanchored -= (float)maxDelay;
              mStutPtr[s+3] = reanchored;
              if (mStutIter[s+3] > 0) mStutIter[s+3]--;
            }
          }

          // Horizontal-sum NEON accumulators into scalar wet/fb.
          {
            float32x2_t loHi = vadd_f32(vget_low_f32(wetLAcc),
                                        vget_high_f32(wetLAcc));
            wetL += vget_lane_f32(vpadd_f32(loHi, loHi), 0);
          }
          {
            float32x2_t loHi = vadd_f32(vget_low_f32(wetRAcc),
                                        vget_high_f32(wetRAcc));
            wetR += vget_lane_f32(vpadd_f32(loHi, loHi), 0);
          }
          {
            float32x2_t loHi = vadd_f32(vget_low_f32(fbAcc),
                                        vget_high_f32(fbAcc));
            fbSum += vget_lane_f32(vpadd_f32(loHi, loHi), 0);
          }

          // Scalar tail.
          for (; s < activeStutterCount; s++)
          {
            float ptr = mStutPtr[s];
            if (ptr >= (float)maxDelay) ptr -= (float)maxDelay;
            int iptr  = (int)ptr;
            int iptr2 = iptr + 1;
            if (iptr2 >= maxDelay) iptr2 -= maxDelay;
            const float frac = ptr - (float)iptr;
            const float a = (float)buf[iptr]  * scale;
            const float b = (float)buf[iptr2] * scale;
            const float sample = a + (b - a) * frac;

            wetL  += sample * mStutGainL[s];
            wetR  += sample * mStutGainR[s];
            fbSum += sample * mStutFbW[s];

            const float speed = mStutSpeed[s];
            mStutPtr[s]       = ptr + speed;
            mStutPosInLoop[s] += speed;
            if (mStutPosInLoop[s] >= mStutLoopSamples[s])
            {
              mStutPosInLoop[s] -= mStutLoopSamples[s];
              float reanchored =
                (float)mStutAnchor[s] + mStutPosInLoop[s];
              if (reanchored >= (float)maxDelay)
                reanchored -= (float)maxDelay;
              mStutPtr[s] = reanchored;
              if (mStutIter[s] > 0) mStutIter[s]--;
            }
          }
        }
#else
        // Scalar fallback (non-NEON build).
        for (int s = 0; s < activeStutterCount; s++)
        {
          float ptr = mStutPtr[s];
          if (ptr >= (float)maxDelay) ptr -= (float)maxDelay;
          int iptr  = (int)ptr;
          int iptr2 = iptr + 1;
          if (iptr2 >= maxDelay) iptr2 -= maxDelay;
          const float frac = ptr - (float)iptr;
          const float a = (float)buf[iptr]  * scale;
          const float b = (float)buf[iptr2] * scale;
          const float sample = a + (b - a) * frac;

          wetL  += sample * mStutGainL[s];
          wetR  += sample * mStutGainR[s];
          fbSum += sample * mStutFbW[s];

          const float speed = mStutSpeed[s];
          mStutPtr[s]       = ptr + speed;
          mStutPosInLoop[s] += speed;
          if (mStutPosInLoop[s] >= mStutLoopSamples[s])
          {
            mStutPosInLoop[s] -= mStutLoopSamples[s];
            float reanchored =
              (float)mStutAnchor[s] + mStutPosInLoop[s];
            if (reanchored >= (float)maxDelay)
              reanchored -= (float)maxDelay;
            mStutPtr[s] = reanchored;
            if (mStutIter[s] > 0) mStutIter[s]--;
          }
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
        //
        // Progressive g per stage: gentler at start (less ringing,
        // more "blur"), stronger at end (more recursive scrambling)
        // — matches Schroeder's original recommendation.
        float diffused = networkAllpassStep(fbDc,    mApBuf1, kNetworkAp1Len, mApIdx1, 0.55f);
        diffused       = networkAllpassStep(diffused, mApBuf2, kNetworkAp2Len, mApIdx2, 0.65f);
        diffused       = networkAllpassStep(diffused, mApBuf3, kNetworkAp3Len, mApIdx3, 0.70f);
        diffused       = networkAllpassStep(diffused, mApBuf4, kNetworkAp4Len, mApIdx4, 0.75f);
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

        // Feed per-sample mono mix into the overview graphic's
        // phase-space ring buffer.
        mOutputRing[mOutputRingPos] = 0.5f * (outDcL + outDcR);
        mOutputRingPos = (mOutputRingPos + 1) & (kOutputRingSize - 1);
      }

      // Block-rate listener trace write (one phase per block).
      mListenerTrace[mListenerTraceHead] = mWalkerPos;
      mListenerTraceHead =
        (mListenerTraceHead + 1) & (kListenerTraceSize - 1);

      // ---- Scatter mutable stutter state back to per-tap arrays ----
      // ptr, posInLoop, iter were mutated by the per-sample stutter
      // pass. Read-only fields (loopSamples, speed, anchor, gains,
      // fbW) didn't change so no scatter for those.
      for (int s = 0; s < activeStutterCount; s++)
      {
        const int t = activeStutterTaps[s];
        mTapStutterReadPtr[t]    = mStutPtr[s];
        mTapStutterPosInLoop[t]  = mStutPosInLoop[s];
        mTapStutterIterations[t] = mStutIter[s];
      }
    }

    // Phase A: sort reflectors by squared distance from orbit
    // center (0,0), assign sorted ranks to group/slot, and hash
    // per-(rank) intra-group offsets from mCascadeSeed. Called
    // from the constructor and on every mSeed change. Cheap —
    // 64-element insertion sort + 64 hash ops, off the audio
    // hot path. Uses squared distance to avoid sqrtf (which on
    // am335x from a package .so can miscompute per the trig-LUT
    // memory rule; ordering is identical for sqrt and sqr).
    void recomputeCascadeAssignment()
    {
      int sortedIdx[kMaxNetworkTaps];
      float sortedDsq[kMaxNetworkTaps];
      for (int i = 0; i < kMaxNetworkTaps; i++)
      {
        sortedIdx[i]  = i;
        sortedDsq[i]  = mReflectors[i].x * mReflectors[i].x +
                        mReflectors[i].y * mReflectors[i].y;
      }
      // Insertion sort ascending by squared distance. Stable.
      for (int i = 1; i < kMaxNetworkTaps; i++)
      {
        const float kd = sortedDsq[i];
        const int   ki = sortedIdx[i];
        int j = i - 1;
        while (j >= 0 && sortedDsq[j] > kd)
        {
          sortedDsq[j + 1] = sortedDsq[j];
          sortedIdx[j + 1] = sortedIdx[j];
          j--;
        }
        sortedDsq[j + 1] = kd;
        sortedIdx[j + 1] = ki;
      }
      // Assign groups by sorted rank.
      for (int rank = 0; rank < kMaxNetworkTaps; rank++)
      {
        const int origIdx = sortedIdx[rank];
        const int g       = rank / kNetworkGroupSize;
        const int slot    = rank % kNetworkGroupSize;
        mTapGroupMap[origIdx]  = (uint8_t)g;
        mTapGroupSlot[origIdx] = (uint8_t)slot;
      }
      // Per-tap intra-group offset, hashed from (g, slot) so the
      // structure is stable per mCascadeSeed regardless of which
      // reflector ends up at that rank.
      for (int t = 0; t < kMaxNetworkTaps; t++)
      {
        const int g    = (int)mTapGroupMap[t];
        const int slot = (int)mTapGroupSlot[t];
        uint32_t h = mCascadeSeed ^
          ((uint32_t)(g * kNetworkGroupSize + slot) * 2654435761u);
        h = h * 1103515245u + 12345u;
        const float u =
          (float)((h >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
        mTapIntraGroupOffset[t] = 0.1f + 0.8f * u;   // [0.1, 0.9]
      }
      // Per-group pool sign. Mirrors the legacy fbWeight sign-flip
      // mechanism: each group contributes to the global pool with a
      // hash-derived ±1 multiplier so coherent components (especially
      // the LP-biased low end of each group's local fb) don't sum
      // constructively across groups. Stable per mCascadeSeed.
      for (int g = 0; g < kNetworkNumGroupsMax; g++)
      {
        uint32_t hs = mCascadeSeed ^
          ((uint32_t)(g + 0xA00) * 2654435761u);
        hs = hs * 1103515245u + 12345u;
        mGroupPoolSign[g] = ((hs >> 16) & 1u) ? 1.0f : -1.0f;
      }
    }

    bool allocate(int Ns)
    {
      deallocate();
      // Cascade buffer sizing: each group gets a full Ns sub-window,
      // not Ns/16. The earlier Ns/16 split capped any single tap's
      // delay at sizeSq × 0.9 × Ns/16 ≈ 56 ms at size=1 — chorus
      // range, not echo. With full per-group Ns, group 15 can reach
      // sizeSq × 0.9 × Ns ≈ 0.9s at size=1, restoring legacy delay
      // range while keeping the cascade's per-group write isolation.
      // Memory cost: Ns × 16 × 2 bytes (≈1.5 MB at 1s @ 48kHz);
      // well within the am335x 64 MB DDR budget.
      const int totalSamples = Ns * kNetworkNumGroupsMax;
      const int nbytes = totalSamples * sizeof(int16_t);
      mBuffer = new (std::nothrow) char[nbytes];
      if (mBuffer)
        memset(mBuffer, 0, nbytes);

      for (int g = 0; g < kNetworkNumGroupsMax; g++)
      {
        mGroupOrigin[g]     = g * Ns;
        mGroupLen[g]        = Ns;
        mGroupWriteIndex[g] = 0;
      }
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
    // Smoothed gain (target) values — pan tracking, density-change
    // fade-in/out. Delay smoothing has been replaced by the dual-read
    // crossfade pattern (mTapOldReadIdx / mTapNewReadIdx below) so
    // there's no mTapDelaySmoothed array anymore. Math credit:
    // ER-301 builtin Delay (mods/core/objects/delays/Delay.cpp:184),
    // PDF design notes (planning/refs/multitap-comb-design-notes.pdf).
    float mTapGainLSmoothed[kMaxNetworkTaps];
    float mTapGainRSmoothed[kMaxNetworkTaps];

    // Dual read indices per tap. Per ER-301 builtin Delay pattern
    // (Doppler-free crossfading delay): each block we shift
    // mTapOldReadIdx[t] = mTapNewReadIdx[t] (carry-over) and compute
    // a new mTapNewReadIdx[t] from the current geometry-derived
    // integer delay. Within the block, both indices advance by 1
    // per sample (no rate slewing, no Doppler chirp). Pass C
    // crossfades from sA (old read) to sB (new read) via a per-
    // sample weight w that ramps 1 → 0 across the block — every
    // block boundary triggers a one-block-long fade, fades chain
    // continuously without gap.
    int32_t mTapOldReadIdx[kMaxNetworkTaps];
    int32_t mTapNewReadIdx[kMaxNetworkTaps];
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

    // Per-tap delay LFO. Each tap has its own phase + per-block-
    // computed rate. Base rate kLfoHz × (1 + seedHash × spread), where
    // spread always has a ±20% baseline plus up to ±50% scaled by
    // motion — so even at motion=0 taps have some rate variation
    // (chorus character), and at motion=1 the field has true
    // polyphonic divergence rather than synchronized swirl.
    // Rates updated each block from motion + seed (block-rate cost
    // negligible). Ring reverb pattern (eurorack/rings/dsp/fx/reverb.h:
    // SetLFOFrequency) but per-tap.
    float mLfoPhase[kMaxNetworkTaps];
    float mLfoRate[kMaxNetworkTaps];   // computed block-rate

    // L3 lush half — per-tap one-pole LP filter scaled by decay.
    // Base cutoff maps decay 0..1 logarithmically to 18kHz..3kHz
    // (Rings reverb damping convention, set_lp pattern). Per-tap
    // ±30% cutoff variation hashed from seed gives L3's "random
    // cutoffs" character. Per-sample LP step on tapV before gain/fb
    // accumulation in Pass C.
    float mTapLpState[kMaxNetworkTaps];
    float mTapLpCoeff[kMaxNetworkTaps];   // computed block-rate

    // G1 glitch — per-tap sample-and-hold on tap delay positions.
    // Clock phase advances at motion × glitch-scaled Hz; on wrap,
    // snapshots the current (post-S1, post-S2) delay target. Lerp
    // continuous→snapshot scaled by glitchAmount for smooth fade-in.
    float mTapShClock[kMaxNetworkTaps];
    float mTapShValue[kMaxNetworkTaps];

    // Per-tap effect mode (mutex assignment, cycle-locked).
    uint8_t  mTapEffectMode[kMaxNetworkTaps];

    // G6 reverse — per-tap signed advance for Pass A read-pointer
    // step. +1 = forward (NORMAL/MUTE/STUTTER/CRUSH/SCRUB),
    // -1 = reverse (REVERSE-mode taps). Set per block from mode.
    int32_t  mTapReadAdvance[kMaxNetworkTaps];

    // G2 glitch — per-tap stutter/freeze. Multi-block loops with
    // fractional per-sample read for ×0.5/×1/×2 octave shifts.
    // mTapStutterAnchor is captured at trigger; mTapStutterLoopSamples
    // is total loop length in samples (length × FRAMELENGTH).
    // mTapStutterIterations counts full loop traversals remaining
    // (2..8 triangular by decay). mTapStutterReadPtr is the absolute
    // float buffer pointer; mTapStutterPosInLoop tracks [0, loopSamples).
    // mTapStutterSpeed picks octave: 0.5, 1.0, or 2.0.
    // mTapStutterGainL/R/FbW are static gains captured at trigger.
    // Stutter taps are zeroed in mTapGainL/R/mFbWeight so Pass C
    // contributes 0 (smoother fades the lush body out at trigger);
    // the scalar stutter pass adds correct-speed contribution.
    uint8_t  mTapStutterIterations[kMaxNetworkTaps];
    int      mTapStutterAnchor[kMaxNetworkTaps];
    uint16_t mTapStutterLength[kMaxNetworkTaps];     // blocks (kept for reseed bookkeeping)
    float    mTapStutterLoopSamples[kMaxNetworkTaps];
    float    mTapStutterReadPtr[kMaxNetworkTaps];    // absolute buffer ptr
    float    mTapStutterPosInLoop[kMaxNetworkTaps];  // [0, loopSamples)
    float    mTapStutterSpeed[kMaxNetworkTaps];      // 0.5, 1.0, 2.0
    float    mTapStutterGainL[kMaxNetworkTaps];
    float    mTapStutterGainR[kMaxNetworkTaps];
    float    mTapStutterFbW[kMaxNetworkTaps];

    // G8 glitch — per-tap bitcrush + sample-rate decimate. Subset of
    // taps chosen probabilistically each block; per-affected-tap
    // bit depth and decimate factor seeded. Branchless NEON apply
    // in Pass C: every tap pays the work, crushMask blends crushed
    // vs original (mask=0 → identity, mask=1 → fully crushed).
    // Lifted from mods/spreadsheet/Larets.cpp:265-278.
    float mTapCrushMask[kMaxNetworkTaps];      // 0 or 1, block-rate
    float mTapCrushBitLvl[kMaxNetworkTaps];    // 2^(12 - bitParam·9.5)
    float mTapCrushInvBitLvl[kMaxNetworkTaps]; // 1/bitLvl
    float mTapDecimFactorF[kMaxNetworkTaps];   // hold-and-resample factor (≥1)
    float mTapDecimCounterF[kMaxNetworkTaps];  // per-sample counter
    float mTapDecimHold[kMaxNetworkTaps];      // per-sample held value

    // Glitch RNG — separate from mWalkerLcg so glitch event timing
    // is independent of motion phase.
    uint32_t mGlitchLcg;

    // G4 — input transient detector. Block-rate envelope follower
    // (fast vs slow). When fast envelope shoots above slow ×
    // threshold, fire a transient event that perturbs K random
    // taps with one of {flip fb sign, duck gain, kick read ptr}.
    // K scales with connectivity × glitch.
    float mEnvFast;
    float mEnvSlow;
    int   mTransientCooldown;   // block counter, prevents back-to-back fires

    // G7 — per-tap respawn lifetime. When countdown hits 0, the
    // tap's reflector gets re-randomized and life resets. Rate
    // scales with motion × glitch (motion=0 or glitch=0 → no
    // respawns ever; respawn period ranges from ~500ms at full
    // settings to several seconds at low).
    uint16_t mTapLifeRemaining[kMaxNetworkTaps];

    // ---- Graphic-side accessors state ----
    // mLastActiveTaps is captured at the end of process() so the
    // overview graphic can iterate the right tap range without
    // recomputing from density.
    int mLastActiveTaps;

    // mTapRicochetFlash[t] is set on G4 events (ricochet
    // perturbation) and decremented per process() call. Graphic
    // reads it as a per-tap brightness boost; lasts ~8 blocks
    // (~43ms) before fully decaying. Range [0, kRicochetFlashMax].
    static const uint8_t kRicochetFlashMax = 8;
    uint8_t mTapRicochetFlash[kMaxNetworkTaps];

    // mTapGeomReadIdx[t] is the geometry-derived read index BEFORE
    // G5 scrub offset is applied. Graphic reads (mTapNewReadIdx -
    // mTapGeomReadIdx) modulo maxDelay to recover the scrub offset
    // for SCRUB-mode Z displacement.
    int mTapGeomReadIdx[kMaxNetworkTaps];

    // Size knob captured for graphic — controls disc render scale.
    float mLastSizeNorm;

    // Decay knob captured for graphic — scales comet tail length on
    // the persistence buffer (longer trails at high decay).
    float mLastDecay;

    // Motion knob captured for graphic — scales sonar ping
    // expansion speed; ping spawn cadence is walker-wrap-driven
    // (one ping per revolution, period = 1 / (walkerHz * motion)).
    float mLastMotion;

    // Wet bus output ring buffer for the overview graphic's
    // phase-space layer. Per-sample mono mix written into a 256-
    // entry circular buffer; graphic reads triplets to plot 3D
    // phase-space points (Rauschen pattern).
    static const int kOutputRingSize = 256;
    float mOutputRing[kOutputRingSize];
    int   mOutputRingPos;

    // Listener phase trace ring — 128 recent walker positions
    // sampled at block-rate (~680ms history at 187 blocks/sec;
    // covers the fastest walker cycle ~800ms).
    static const int kListenerTraceSize = 128;
    float mListenerTrace[kListenerTraceSize];
    int   mListenerTraceHead;

    // Stutter NEON scratch (class members for naturally-aligned heap
    // allocation per feedback_neon_intrinsics_drumvoice — stack-local
    // NEON arrays produce trapping :64/:128 hints on Cortex-A8).
    // Populated by block-rate gather from mTapStutter* arrays;
    // mutable fields (Ptr, PosInLoop, Iter) scattered back at end
    // of process().
    static const int kStutScratchSlots = 16;
    float   mStutPtr[kStutScratchSlots];
    float   mStutPosInLoop[kStutScratchSlots];
    float   mStutLoopSamples[kStutScratchSlots];
    float   mStutSpeed[kStutScratchSlots];
    int     mStutAnchor[kStutScratchSlots];
    float   mStutGainL[kStutScratchSlots];
    float   mStutGainR[kStutScratchSlots];
    float   mStutFbW[kStutScratchSlots];
    uint8_t mStutIter[kStutScratchSlots];

    // 4-stage allpass diffusion chain in feedback path. Phase-
    // decorrelates the recycled signal each cycle, breaking the
    // resonance accumulation that the walker alone can't outpace.
    float mApBuf1[kNetworkAp1Len];
    float mApBuf2[kNetworkAp2Len];
    float mApBuf3[kNetworkAp3Len];
    float mApBuf4[kNetworkAp4Len];
    int mApIdx1, mApIdx2, mApIdx3, mApIdx4;

    // ---- Phase A scaffolding for serial cascade rebuild ----
    // See planning/network-cascade-rebuild-plan.md. State is
    // allocated and initialized but NOT yet read by the per-sample
    // loop; the existing star-multitap audio path still produces
    // sound until Phase A step 5 lands the new loop body.

    // Per-instance cascade seed (per-tap intra-group offsets are
    // hashed from this). Static after construction; motion does
    // not modulate it (would Doppler).
    uint32_t mCascadeSeed;

    // Per-group sub-window in the shared mBuffer ring. Origin and
    // length are set in allocate(); writeIndex advances per-sample
    // within [0, mGroupLen[g]).
    int mGroupOrigin    [kNetworkNumGroupsMax];
    int mGroupLen       [kNetworkNumGroupsMax];
    int mGroupWriteIndex[kNetworkNumGroupsMax];

    // Local feedback state per group (one float: previous sample's
    // group_mono_out). Fed back into the group's input multiplied
    // by conn × 0.6 ceiling.
    float mGroupLocalFbState[kNetworkNumGroupsMax];

    // Per-group one-pole DC blocker state. Applied to g_prev_out
    // after the cascade-flow normalization, before the value is
    // used as (a) next group's cascade input, (b) this group's
    // local feedback state, or (c) the global pool contribution.
    // One blocker per group catches DC accumulation from asymmetric
    // tanh saturation in the per-group recirculation; without it
    // sustained high-conn high-decay patches latch into a DC-
    // saturated state. Same R coefficient (kNetworkDcR) and same
    // one-pole topology as the existing input / output / fb DC
    // blockers — idiomatic.
    float mGroupDcX1[kNetworkNumGroupsMax];
    float mGroupDcY1[kNetworkNumGroupsMax];

    // Per-group one-pole LP filter on the local feedback signal.
    // Damps high-frequency content per recirculation so short
    // per-group delays (e.g., size=0.35 → ~140 Hz fundamental)
    // don't behave as undamped Karplus-Strong strings and build
    // up resonant peaks. Same mechanism as Schroeder/Moorer reverb
    // damping and the legacy L3 per-tap LP. Cutoff couples to
    // decay so high decay = more damping (longer sustain stays
    // stable), low decay = less damping (crisp echoes).
    float mGroupFbLpState[kNetworkNumGroupsMax];

    // Per-group Jot attenuation coefficient. Computed block-rate
    // from each group's average tap delay and the user's target T60
    // (mapped from the decay knob): coef = 10^(-3 × D_g / (T60 × Fs)).
    // Each delay line's coefficient is sized so all groups decay at
    // the SAME T60 regardless of their delay length — the canonical
    // Jot 1991 design. Without this, scaling the matrix gain alone
    // gives short-delay groups infinite tails while long-delay
    // groups die instantly (or vice versa) — which was why the
    // user's "decay" knob felt nearly dead even with full-rank
    // Hadamard cross-feed in place: the cross-feed was structurally
    // correct, but T60 cannot be controlled by uniform matrix gain
    // in an FDN with mixed delay lengths.
    float mGroupDecayCoef[kNetworkNumGroupsMax];

    // Hadamard FDN cross-feed scratch. Holds the per-sample
    // FWHT result M[16] = H_16 × G[16], where G is the vector of
    // per-group previous outputs (mGroupLocalFbState). Each group's
    // input receives M[g] as the cross-feed contribution from ALL
    // other groups with orthogonal sign weighting — this is the
    // full-rank cross-feed matrix that converts the cascade from a
    // resonator (rank-1 pool injection) into a proper Feedback Delay
    // Network reverb. Class member (not stack-local) per
    // feedback_neon_intrinsics_drumvoice — stack-local NEON-adjacent
    // arrays risk GCC :64 alignment hints on Cortex-A8.
    float mHadamardScratch[kNetworkNumGroupsMax];

    // Per-group one-pole HPF state on the local feedback path. The
    // existing per-group LP filter integrates DC and low-mid content
    // at very short loop lengths (low size → 1-5 ms loops → LP cutoff
    // sits inside loop bandwidth → effectively a DC accumulator).
    // The per-group DC blocker at ~50 Hz doesn't catch 60-150 Hz
    // buildup; the pool HPF added in 2.6.1.70 only helps pool-path.
    // Apply HPF after the LP on the local fb signal so the loop is
    // band-passed (LP rolls highs, HPF rolls lows → stable middle
    // band). Coefficient kFbHpR ≈ 0.987 → ~100 Hz cutoff at 48 kHz.
    float mGroupFbHpX1[kNetworkNumGroupsMax];
    float mGroupFbHpY1[kNetworkNumGroupsMax];

    // Per-group sign for the global pool contribution. Hashed from
    // mCascadeSeed once per (seed change), so it's stable for a
    // given instance. Mirrors the legacy fbWeight sign-flip
    // mechanism that prevented coherent feedback-bus buildup;
    // without it, the cascade pool sums tail-third group outputs
    // with all-same-sign, and low-frequency components add
    // constructively across groups → audible low-end accumulation
    // at sustained high-conn settings.
    float mGroupPoolSign[kNetworkNumGroupsMax];

    // High-pass on the diffused global pool result before it
    // injects into group 0's input. The 4-stage Schroeder allpass
    // chain (lengths 167/263/419/677 samples @ 48kHz) has modal
    // resonances at 71/115/183/286 Hz — the "low end." The pool's
    // DC blocker (kNetworkDcR ≈ 50 Hz cutoff) doesn't catch these.
    // A second HPF at ~100 Hz applied AFTER diffusion catches the
    // allpass modal buildup that otherwise accumulates over many
    // pool recirculations. R = 0.987 → ~100 Hz cutoff at 48 kHz.
    float mPoolHpX1;
    float mPoolHpY1;

    // Global feedback pool, propagated one sample late (computed
    // this sample after the cascade, injected into group 0 next
    // sample). Stores the post-Schroeder, post-soften value.
    float mDiffusedGlobalPool;

    // Per-tap → group mapping. Sorted by reflector distance at
    // construction and on seed change. mTapGroupMap[t] = which
    // group tap t belongs to (0..NumGroupsMax-1); mTapGroupSlot[t]
    // = which of the 4 lanes within that group.
    uint8_t mTapGroupMap [kMaxNetworkTaps];
    uint8_t mTapGroupSlot[kMaxNetworkTaps];

    // Per-tap intra-group offset fraction in [0.1, 0.9]. Hashed
    // from mCascadeSeed at construction / seed change. Multiplied
    // by mGroupLen[g] to derive the tap's delay position within
    // its group's sub-window.
    float mTapIntraGroupOffset[kMaxNetworkTaps];

    // Per-group stutter accumulators (the scalar stutter pass will
    // route each active stutter tap's contribution into its
    // group's accumulator via mStutGroup[s] — see Phase C).
    float mGroupMonoStutterAcc[kNetworkNumGroupsMax];
    float mGroupStutterWetL   [kNetworkNumGroupsMax];
    float mGroupStutterWetR   [kNetworkNumGroupsMax];

    // Stutter tap's owning group (Phase C will populate this at
    // stutter trigger time). Same length as the other stutter
    // scratch arrays.
    uint8_t mStutGroup[kStutScratchSlots];

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
