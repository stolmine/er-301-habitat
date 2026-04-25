// AlembicVoice -- native 4-op phase-mod matrix voice. See AlembicVoice.h
// for architecture notes. Phase 2a: scalar per-op inner loop; only NEON
// op per sample is a single simd_sin call over the packed phase args.

#include "AlembicVoice.h"
#include <od/config.h>
#include <hal/constants.h>
#include <hal/ops.h>
#include <hal/simd.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static const float kTwoPi = 6.28318530718f;
  static const float kLn2 = 0.69314718056f;
  // Matches libcore SineOscillator.cpp: feedback inlet value is clamped
  // to [-1,1] then multiplied by 0.18 before combining with the phase
  // argument. AlembicRef routes diagonal matrix entries via each op's
  // Feedback inlet (picking up this scale internally); AlembicVoice
  // applies the same 0.18 to diagonal terms in the consolidated matrix
  // sum so the two units produce identical self-feedback math.
  static const float kFbScale = 0.18f;

  // Phase 3 hand-authored preset gradient, stored at FILE scope (shared
  // across all AlembicVoice instances; init-on-first-construction). Slot
  // 0 is a clean sine on op A; slot 63 is 4-op chaos with all matrix
  // entries ~0.45 and op detunes for beating. Linear interpolation
  // across slots 1..62. Phase 5 replaces this with offline training
  // output. ~7.4 KB total (not per-instance).
  //
  // Per-slot layout (29 floats):
  //   [0..3]   ratios
  //   [4..7]   levels
  //   [8..11]  detunes (Hz)
  //   [12..27] matrix[src*4 + dst], source-major
  //   [28]     reagent flag (Phase 7+; ignored in Phase 3)
  static float sPresetTable[64][29];
  static bool sPresetTableInit = false;

  // Disable auto-vectorization on this initializer. GCC otherwise emits
  // NEON quad ops with `:64` alignment hints on the .rodata endA/endB
  // and .bss sPresetTable accesses, plus a stack-spill quad write to
  // `[sp :64]`. The runtime alignment doesn't match the hints on
  // Cortex-A8 and the unit traps at construction time -- exactly the
  // class of crash from feedback_neon_intrinsics_drumvoice but in the
  // constructor instead of the per-sample loop. Scalar code path is
  // perfectly fast for a one-shot 64*29 init.
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void fillPhase3Presets()
  {
    if (sPresetTableInit) return;
    static const float endA[29] = {
        1.0f, 2.0f, 3.0f, 5.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f};
    static const float endB[29] = {
        1.0f, 2.0f, 3.0f, 5.0f,
        0.4f, 0.3f, 0.2f, 0.1f,
        0.0f, 2.0f, 4.0f, 6.0f,
        0.45f, 0.45f, 0.45f, 0.45f,
        0.45f, 0.45f, 0.45f, 0.45f,
        0.45f, 0.45f, 0.45f, 0.45f,
        0.45f, 0.45f, 0.45f, 0.45f,
        0.0f};
    for (int slot = 0; slot < 64; slot++)
    {
      const float u = (float)slot / 63.0f;
      for (int f = 0; f < 29; f++)
      {
        sPresetTable[slot][f] = endA[f] + u * (endB[f] - endA[f]);
      }
    }
    sPresetTableInit = true;
  }

  AlembicVoice::AlembicVoice()
  {
    addInput(mVOct);
    addInput(mSync);
    addOutput(mOut);

    addParameter(mF0);
    addParameter(mGlobalLevel);
    addParameter(mScanPos);
    addParameter(mScanK);

    addParameter(mRatioA);
    addParameter(mRatioB);
    addParameter(mRatioC);
    addParameter(mRatioD);

    addParameter(mLevelA);
    addParameter(mLevelB);
    addParameter(mLevelC);
    addParameter(mLevelD);

    addParameter(mDetuneA);
    addParameter(mDetuneB);
    addParameter(mDetuneC);
    addParameter(mDetuneD);

    addParameter(mMatrixAA);
    addParameter(mMatrixAB);
    addParameter(mMatrixAC);
    addParameter(mMatrixAD);
    addParameter(mMatrixBA);
    addParameter(mMatrixBB);
    addParameter(mMatrixBC);
    addParameter(mMatrixBD);
    addParameter(mMatrixCA);
    addParameter(mMatrixCB);
    addParameter(mMatrixCC);
    addParameter(mMatrixCD);
    addParameter(mMatrixDA);
    addParameter(mMatrixDB);
    addParameter(mMatrixDC);
    addParameter(mMatrixDD);

    memset(mPhaseBank, 0, sizeof(mPhaseBank));
    memset(mPrevOutBank, 0, sizeof(mPrevOutBank));
    memset(mPhaseArgBank, 0, sizeof(mPhaseArgBank));
    memset(mSineBank, 0, sizeof(mSineBank));
    memset(mMatrixFlat, 0, sizeof(mMatrixFlat));
    memset(mRatioFlat, 0, sizeof(mRatioFlat));
    memset(mDetuneFlat, 0, sizeof(mDetuneFlat));
    memset(mLevelFlat, 0, sizeof(mLevelFlat));
    fillPhase3Presets();
    mSyncWasHigh = false;
  }

  AlembicVoice::~AlembicVoice()
  {
  }

  void AlembicVoice::process()
  {
    const float sr = globalConfig.sampleRate;
    const float sp = globalConfig.samplePeriod;
    // glog2 = FULLSCALE_IN_VOLTS * ln2, matching SineOscillator.cpp:41.
    const float glog2 = FULLSCALE_IN_VOLTS * kLn2;
    const float nyquist = sr * 0.49f;

    float *out = mOut.buffer();
    float *vOct = mVOct.buffer();
    float *sync = mSync.buffer();

    const float f0 = CLAMP(0.01f, nyquist, mF0.value());
    const float lvl = mGlobalLevel.value();

    // Phase 3 K-weighted preset crossfade. Block-rate. Output buffers are
    // class members (mMatrixFlat / mRatioFlat / mDetuneFlat / mLevelFlat),
    // same arrays the per-sample NEON loop reads from in Phase 2b -- no
    // changes to that loop. Rest of process() is identical to Phase 2b.
    const float scanPos = CLAMP(0.0f, 1.0f, mScanPos.value());
    const int K = (int)CLAMP(2.0f, 6.0f, mScanK.value() + 0.5f);
    const float s = scanPos * 63.0f;
    const float halfWidth = 0.5f * (float)K;

    // Pick K nodes around s, clamping at table edges.
    int n0 = (int)(s + 0.5f) - K / 2;
    if (n0 < 0) n0 = 0;
    if (n0 + K > 64) n0 = 64 - K;

    // Triangular weights, normalize to sum=1. K up to 6.
    float w[6];
    float wSum = 0.0f;
    for (int j = 0; j < K; j++)
    {
      const float dist = fabsf((float)(n0 + j) - s);
      const float wRaw = halfWidth - dist;
      w[j] = wRaw > 0.0f ? wRaw : 0.0f;
      wSum += w[j];
    }
    if (wSum < 1e-9f)
    {
      w[0] = 1.0f;
      wSum = 1.0f;
    }
    const float wInv = 1.0f / wSum;

    // Blend the 29 per-slot floats into the working voice state buffers.
    // Loop body is uniform across K (no differential dispatch per
    // feedback_runtime_branched_dsp_dispatch).
    for (int f = 0; f < 4; f++) mRatioFlat[f] = 0.0f;
    for (int f = 0; f < 4; f++) mLevelFlat[f] = 0.0f;
    for (int f = 0; f < 4; f++) mDetuneFlat[f] = 0.0f;
    for (int f = 0; f < 16; f++) mMatrixFlat[f] = 0.0f;
    for (int j = 0; j < K; j++)
    {
      const float wj = w[j] * wInv;
      const float *p = sPresetTable[n0 + j];
      for (int f = 0; f < 4; f++) mRatioFlat[f] += wj * p[f];
      for (int f = 0; f < 4; f++) mLevelFlat[f] += wj * p[4 + f];
      for (int f = 0; f < 4; f++) mDetuneFlat[f] += wj * p[8 + f];
      for (int f = 0; f < 16; f++) mMatrixFlat[f] += wj * p[12 + f];
    }
    // Apply the diagonal kFbScale after the blend, matching Phase 2b's
    // per-sample matrix-sum convention. Off-diagonal cross-mod stays 1x.
    mMatrixFlat[0 * 4 + 0] *= kFbScale;
    mMatrixFlat[1 * 4 + 1] *= kFbScale;
    mMatrixFlat[2 * 4 + 2] *= kFbScale;
    mMatrixFlat[3 * 4 + 3] *= kFbScale;

    // Block-rate NEON loads. Only vLevel hoists across the per-sample
    // loop -- it's used in the output-sum step *after* simd_sin so it
    // can sit in a callee-save NEON register naturally. The other
    // quads (vRow0..3, vRatioF0Sp, vDetuneSp) are loaded INSIDE the
    // loop, just before their use, and become dead before simd_sin.
    // This avoids GCC having to spill them to stack across the
    // simd_sin call -- the spill emitted `vst1.64 {dN}, [sp :64]` hints
    // which trap on misaligned stack offsets per
    // feedback_neon_intrinsics_drumvoice. Per-sample re-load cost is
    // ~6 quad ops, marginal vs the simd_sin and matrix-FMA cost.
    const float32x4_t vLevel = vld1q_f32(mLevelFlat);
    const float f0Sp = f0 * sp;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // V/Oct: scalar expf per sample. Vectorizing expf via simd_exp
      // would require a 4-sample unroll, which conflicts with the
      // sample-serial feedback dependency (each sample's matrix*prev
      // reads the previous sample's sine output). Scalar expf is ~50
      // cycles/sample; total per-sample is well under budget.
      float q = vOct[i];
      if (q > 1.0f) q = 1.0f;
      if (q < -1.0f) q = -1.0f;
      const float ex = expf(q * glog2);

      // Phase advance (NEON 4-wide):
      //   phase[op] += (f0 * ratio[op] * sp) * ex + (detune[op] * sp)
      // Build vRatioF0Sp / vDetuneSp inside the loop so they don't
      // need to survive simd_sin further down.
      const float32x4_t vRatioF0Sp = vmulq_n_f32(vld1q_f32(mRatioFlat), f0Sp);
      const float32x4_t vDetuneSp  = vmulq_n_f32(vld1q_f32(mDetuneFlat), sp);
      float32x4_t vPhase = vld1q_f32(mPhaseBank);
      vPhase = vaddq_f32(vPhase, vmlaq_n_f32(vDetuneSp, vRatioF0Sp, ex));

      // Sync: hard reset on rising edge. Threshold > 0.5f per
      // feedback_comparator_gate_threshold.
      const bool high = sync[i] > 0.5f;
      if (high && !mSyncWasHigh)
      {
        vPhase = vdupq_n_f32(0.0f);
      }
      mSyncWasHigh = high;

      // Wrap phase to [0,1). vcvtq_s32_f32 truncates toward zero; equals
      // floor only for non-negative inputs. Phases monotonically
      // increase from 0 and are only reset to 0 via sync, so the
      // non-negative invariant holds.
      const float32x4_t vInt = vcvtq_f32_s32(vcvtq_s32_f32(vPhase));
      vPhase = vsubq_f32(vPhase, vInt);
      vst1q_f32(mPhaseBank, vPhase);

      // Matrix * prevOut via column-broadcast FMA chain. Load matrix
      // rows inside the loop (block-invariant but cheap to re-load)
      // so they don't have to survive simd_sin -- avoids the
      // `[sp :64]` NEON spill trap.
      const float32x4_t vRow0 = vld1q_f32(&mMatrixFlat[0]);
      const float32x4_t vRow1 = vld1q_f32(&mMatrixFlat[4]);
      const float32x4_t vRow2 = vld1q_f32(&mMatrixFlat[8]);
      const float32x4_t vRow3 = vld1q_f32(&mMatrixFlat[12]);
      float32x4_t vArg = vmulq_n_f32(vRow0, mPrevOutBank[0]);
      vArg = vmlaq_n_f32(vArg, vRow1, mPrevOutBank[1]);
      vArg = vmlaq_n_f32(vArg, vRow2, mPrevOutBank[2]);
      vArg = vmlaq_n_f32(vArg, vRow3, mPrevOutBank[3]);
      vArg = vaddq_f32(vArg, vPhase);
      vst1q_f32(mPhaseArgBank, vArg);

      // Vectorized sine via simd_sin over 4 packed args. Unchanged from
      // phase 2a; this is the same Cephes polynomial libcore uses.
      float32x4_t vSine = simd_sin(vmulq_n_f32(vArg, kTwoPi));
      vst1q_f32(mSineBank, vSine);

      // Output sum: NEON mul then horizontal reduce to scalar, scaled by
      // the global level. vadd(low, high) + vpadd is the 4->1 reduction
      // pattern on ARMv7 NEON (no vaddvq_f32 on Cortex-A8).
      float32x4_t vProd = vmulq_f32(vSine, vLevel);
      float32x2_t vPair = vadd_f32(vget_low_f32(vProd), vget_high_f32(vProd));
      vPair = vpadd_f32(vPair, vPair);
      out[i] = vget_lane_f32(vPair, 0) * lvl;

      // Prev update for next sample's matrix/feedback.
      vst1q_f32(mPrevOutBank, vSine);
    }
  }

} // namespace stolmine
