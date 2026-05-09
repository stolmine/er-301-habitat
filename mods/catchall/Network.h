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
        mTapStutterRemaining[i] = 0;  // G2 stutter: 0 = NORMAL
        mTapStutterAnchor[i] = 0;     // G2 stutter anchor (read idx)
        mTapStutterLength[i] = 0;     // G2 loop length (blocks)
        mTapStutterPos[i] = 0;        // G2 position within loop
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

      // ---- Block-rate geometry recompute ----
      // Density-compensated gain: per-tap magnitude scales as
      // C/√activeTaps so summed wet RMS is roughly constant as
      // density sweeps (purely a structural / spatial-richness
      // control). The constant C controls absolute output level —
      // 1.0 was too quiet relative to chain headroom; 2.5 brings
      // peak-to-peak output to a usable range without exceeding
      // tanh saturation in the wet bus (the fb path uses its own
      // 1/√k normalization independent of this constant).
      const float densityCompGain = 2.5f / sqrtf((float)activeTaps);
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
      const float kCutoffRatio = 6.0f;   // 18k / 3k
      const float baseCutoffHz =
        kMaxCutoffHz * expf(-decay * logf(kCutoffRatio));
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

      // G3: probabilistic mute. Density gates eligibility, glitch
      // scales probability. Linear with floor: at glitch tip-on the
      // probability jumps to minP, then ramps to maxP at glitch=1.
      // Scaled to 16-bit threshold for cheap unsigned compare.
      const float kMuteMinP = 0.02f;
      const float kMuteMaxP = 0.40f;
      const float mutePf = (glitchAmount > 0.0f)
        ? density * (kMuteMinP + (kMuteMaxP - kMuteMinP) * glitchAmount)
        : 0.0f;
      const uint32_t muteThresh = (uint32_t)(mutePf * 65535.0f);

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

        // G3 — probabilistic mute. Multiplicative gain mask; smoother
        // ramps to silence over the block, no clicks. Decision is
        // re-evaluated each block (mGlitchLcg advances), so muted
        // taps don't stick.
        if (muteThresh > 0u)
        {
          uint32_t hMute = mGlitchLcg ^ ((uint32_t)i * 2654435761u + 0x33333333u);
          hMute = hMute * 1103515245u + 12345u;
          const uint32_t muteRand = (hMute >> 16) & 0xFFFFu;
          if (muteRand < muteThresh)
          {
            mTapGainL[i] = 0.0f;
            mTapGainR[i] = 0.0f;
          }
        }

        // L3 per-tap LP coefficient. ±30% cutoff variation around
        // the decay-driven base. Different XOR mask again to keep
        // uncorrelated with detune / rate hashes.
        uint32_t hLp = mLastSeed ^ ((uint32_t)i * 2654435761u + 0x77777777u);
        hLp = hLp * 1103515245u + 12345u;
        const float lpVariation =
          0.7f + 0.6f * ((float)((hLp >> 16) & 0xFFFFu) * (1.0f / 65535.0f));
        float coeff = baseCoeff * lpVariation;
        if (coeff > 1.0f) coeff = 1.0f;   // stability clamp
        if (coeff < 0.0f) coeff = 0.0f;
        mTapLpCoeff[i] = coeff;
      }

      // ---- G8 — bitcrush + decimate subset (block-rate setup) ----
      // Per-tap subset chosen probabilistically: frac = density ×
      // maxFrac × glitch, capped at 60% so we never reach 100%
      // coverage. Per-affected-tap bit depth and decimate factor
      // sampled from full Larets ranges (bitParam ∈ [0,1] →
      // bitLvl ∈ [4096 (12-bit), 5.66 (~2.5-bit)]; decimParam ∈
      // [0,1] → factor ∈ [1, 32]) but each sampled via triangular
      // distribution with mode=density: low density → severities
      // bias toward gentle, high density → toward heavy. Both
      // extremes always reachable regardless of density (the
      // continuum rule).
      const float kCrushMaxFrac = 0.6f;
      const float crushFrac = density * kCrushMaxFrac * glitchAmount;
      const uint32_t crushFracThresh =
        (uint32_t)(crushFrac * 65535.0f);
      for (int i = 0; i < activeTaps; i++)
      {
        // Subset pick — same hash seed style as other glitch
        // primitives, with G8's XOR mask.
        uint32_t hCrush = mGlitchLcg ^
          ((uint32_t)i * 2654435761u + 0xC3C3C3C3u);
        hCrush = hCrush * 1103515245u + 12345u;
        const uint32_t crushRand = (hCrush >> 16) & 0xFFFFu;
        if (glitchAmount > 0.0f && crushRand < crushFracThresh)
        {
          mTapCrushMask[i] = 1.0f;

          // Per-tap bit depth — Larets full range, triangular
          // sampled with mode=density.
          hCrush = hCrush * 1103515245u + 12345u;
          const float bitU =
            (float)((hCrush >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
          const float bitParam = networkTriangularSample(bitU, density);
          const float bitLvl = powf(2.0f, 12.0f - bitParam * 9.5f);
          mTapCrushBitLvl[i] = bitLvl;
          mTapCrushInvBitLvl[i] = 1.0f / bitLvl;

          // Per-tap decimate factor — Larets full range [1, 32],
          // triangular sampled with mode=density.
          hCrush = hCrush * 1103515245u + 12345u;
          const float decimU =
            (float)((hCrush >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
          const float decimParam = networkTriangularSample(decimU, density);
          int factor = 1 + (int)(decimParam * 31.0f);
          mTapDecimFactorF[i] = (float)factor;
        }
        else
        {
          // Un-crushed: identity values. crushMask=0 makes the
          // per-sample blend a no-op regardless of crushed branch.
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
      }

      // ---- G2 stutter/freeze override ----
      // Per-tap state machine evaluated at block-rate. NORMAL taps
      // can transition to STUTTER via probabilistic trigger. STUTTER
      // taps loop over a multi-block window:
      //   readIdx[block] = anchor + pos * FRAMELENGTH (mod maxDelay)
      //   pos = (pos + 1) mod length
      // Loop length is musical: 24..96 blocks → ~125ms..512ms
      // (16th-note..quarter-note @120BPM). Triangular-distributed
      // with mode=decay so low decay → expected loop length near
      // 16th-note, high decay → expected near quarter-note. Both
      // extremes always reachable. Duration also triangular-decay
      // mapped to 2..32 blocks (long enough for at least one full
      // cycle of the longest loops).
      //
      // Trigger probability scales with decay × glitch (linear with
      // floor; long-decay reverbs hold stutter audibly longer).
      const float kStutterMinTrigP = 0.005f;
      const float kStutterMaxTrigP = 0.05f;
      const float stutterPf = (glitchAmount > 0.0f)
        ? decay * (kStutterMinTrigP +
                   (kStutterMaxTrigP - kStutterMinTrigP) * glitchAmount)
        : 0.0f;
      const uint32_t stutterTriggerThresh =
        (uint32_t)(stutterPf * 65535.0f);
      const int kStutterMinLenBlocks = 24;   // ~125ms (16th @ 120BPM)
      const int kStutterMaxLenBlocks = 96;   // ~512ms (quarter @ 120BPM)
      const int kStutterMinDurBlocks = 2;
      const int kStutterMaxDurBlocks = 32;
      if (glitchAmount > 0.0f)
      {
        for (int t = 0; t < activeTaps; t++)
        {
          if (mTapStutterRemaining[t] > 0)
          {
            // Currently stuttering — read at anchor + pos*FRAME.
            // Pass A then advances per-sample to anchor +
            // (pos+1)*FRAME by block end (continuous in-loop), then
            // next block's override jumps back when pos wraps.
            int idx = mTapStutterAnchor[t] +
                      (int)mTapStutterPos[t] * (int)FRAMELENGTH;
            // Wrap into buffer (loop length × FRAMELENGTH ≤
            // maxDelay/2 by config, so single subtraction suffices,
            // but loop in case of edge cases).
            while (idx >= maxDelay) idx -= maxDelay;
            while (idx < 0) idx += maxDelay;
            mTapNewReadIdx[t] = idx;
            mTapOldReadIdx[t] = idx;
            // Advance pos within loop.
            uint16_t nextPos = (uint16_t)(mTapStutterPos[t] + 1u);
            if (nextPos >= mTapStutterLength[t]) nextPos = 0;
            mTapStutterPos[t] = nextPos;
            mTapStutterRemaining[t]--;
          }
          else if (stutterTriggerThresh > 0u)
          {
            // NORMAL: hash decision to enter stutter.
            uint32_t hStut = mGlitchLcg ^
              ((uint32_t)t * 2654435761u + 0x66666666u);
            hStut = hStut * 1103515245u + 12345u;
            const uint32_t triggerRand = (hStut >> 16) & 0xFFFFu;
            if (triggerRand < stutterTriggerThresh)
            {
              // Capture anchor at current read position.
              mTapStutterAnchor[t] = mTapNewReadIdx[t];

              // Loop length — triangular with mode=decay.
              hStut = hStut * 1103515245u + 12345u;
              const float lenU =
                (float)((hStut >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
              const float lenShaped = networkTriangularSample(lenU, decay);
              const int lenBlocks = kStutterMinLenBlocks +
                (int)((kStutterMaxLenBlocks - kStutterMinLenBlocks)
                      * lenShaped + 0.5f);
              mTapStutterLength[t] = (uint16_t)lenBlocks;
              mTapStutterPos[t] = 0;

              // Duration — triangular with mode=decay.
              hStut = hStut * 1103515245u + 12345u;
              const float durU =
                (float)((hStut >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
              const float durShaped = networkTriangularSample(durU, decay);
              const int durBlocks = kStutterMinDurBlocks +
                (int)((kStutterMaxDurBlocks - kStutterMinDurBlocks)
                      * durShaped + 0.5f);
              mTapStutterRemaining[t] = (uint8_t)durBlocks;

              // Apply override this block (pos=0).
              mTapOldReadIdx[t] = mTapStutterAnchor[t];
              mTapNewReadIdx[t] = mTapStutterAnchor[t];
              mTapStutterPos[t] = 1;
              mTapStutterRemaining[t]--;
            }
          }
        }
      }
      else
      {
        // Glitch off: clear stutter state so re-engagement is fresh.
        for (int t = 0; t < kMaxNetworkTaps; t++)
        {
          mTapStutterRemaining[t] = 0;
          mTapStutterPos[t] = 0;
          mTapStutterLength[t] = 0;
        }
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

      // ---- Scratch arrays for 3-pass tap processing ----
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
        // Doppler-free: each tap's old/new read indices increment by
        // 1 per sample, wrap at maxDelay. No idx-from-delay math, no
        // fractional interp. The ER-301 builtin Delay does this same
        // pattern (Delay.cpp:209-219).
#ifdef NETWORK_HAS_NEON
        {
          const int32x4_t maxDelayVec = vdupq_n_s32(maxDelay);
          const int32x4_t oneVec = vdupq_n_s32(1);
          const int32x4_t zeroIVec = vdupq_n_s32(0);

          int t = 0;
          for (; t + 4 <= activeTaps; t += 4)
          {
            // Old read
            int32x4_t oldIdx = vld1q_s32(&mTapOldReadIdx[t]);
            oldIdx = vaddq_s32(oldIdx, oneVec);
            uint32x4_t oldWrap = vcgeq_s32(oldIdx, maxDelayVec);
            oldIdx = vbslq_s32(oldWrap, zeroIVec, oldIdx);
            vst1q_s32(&mTapOldReadIdx[t], oldIdx);

            // New read
            int32x4_t newIdx = vld1q_s32(&mTapNewReadIdx[t]);
            newIdx = vaddq_s32(newIdx, oneVec);
            uint32x4_t newWrap = vcgeq_s32(newIdx, maxDelayVec);
            newIdx = vbslq_s32(newWrap, zeroIVec, newIdx);
            vst1q_s32(&mTapNewReadIdx[t], newIdx);
          }
          for (; t < activeTaps; t++)
          {
            int o = mTapOldReadIdx[t] + 1;
            if (o >= maxDelay) o = 0;
            mTapOldReadIdx[t] = o;
            int n = mTapNewReadIdx[t] + 1;
            if (n >= maxDelay) n = 0;
            mTapNewReadIdx[t] = n;
          }
        }
#else
        for (int t = 0; t < activeTaps; t++)
        {
          int o = mTapOldReadIdx[t] + 1;
          if (o >= maxDelay) o = 0;
          mTapOldReadIdx[t] = o;
          int n = mTapNewReadIdx[t] + 1;
          if (n >= maxDelay) n = 0;
          mTapNewReadIdx[t] = n;
        }
#endif

        // ---- Pass B (scalar gather + 8-ahead prefetch) ----
        // sA from old read pointer, sB from new read pointer.
        for (int t = 0; t < activeTaps; t++)
        {
          int pfIdx = t + 8;
          if (pfIdx < activeTaps)
            __builtin_prefetch(&buf[mTapOldReadIdx[pfIdx]], 0, 1);
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

    // G2 glitch — per-tap stutter/freeze. Multi-block loops:
    // mTapStutterAnchor is captured at trigger; mTapStutterLength
    // is total loop length in blocks (musical: 24..96 blocks =
    // ~125ms..512ms = 16th-note..quarter-note @120BPM); mTapStutterPos
    // is the current block index within the loop [0, length).
    // mTapStutterRemaining is duration countdown (block units, 2..32).
    // Each stuttered block sets both read indices to
    //   anchor + pos*FRAMELENGTH (mod maxDelay)
    // and increments pos mod length. Loop discontinuity is the
    // stutter character; in-loop block transitions are continuous.
    uint8_t  mTapStutterRemaining[kMaxNetworkTaps];
    int      mTapStutterAnchor[kMaxNetworkTaps];
    uint16_t mTapStutterLength[kMaxNetworkTaps];
    uint16_t mTapStutterPos[kMaxNetworkTaps];

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
