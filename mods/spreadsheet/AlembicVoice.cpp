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

  AlembicVoice::AlembicVoice()
  {
    addInput(mVOct);
    addInput(mSync);
    addOutput(mOut);

    addParameter(mF0);
    addParameter(mGlobalLevel);

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

    // Populate block-rate scratch arrays (class members, heap-allocated
    // with the instance). Per feedback_neon_intrinsics_drumvoice this is
    // the storage class that guarantees the subsequent vld1q emits the
    // unaligned-safe `vld1.32 [reg]` form (no `:64` hint) on Cortex-A8.
    // Stack-local float[4] locals would trap via hint promotion under
    // -O3 -ffast-math.
    mRatioFlat[0] = mRatioA.value();
    mRatioFlat[1] = mRatioB.value();
    mRatioFlat[2] = mRatioC.value();
    mRatioFlat[3] = mRatioD.value();

    mLevelFlat[0] = mLevelA.value();
    mLevelFlat[1] = mLevelB.value();
    mLevelFlat[2] = mLevelC.value();
    mLevelFlat[3] = mLevelD.value();

    mDetuneFlat[0] = mDetuneA.value();
    mDetuneFlat[1] = mDetuneB.value();
    mDetuneFlat[2] = mDetuneC.value();
    mDetuneFlat[3] = mDetuneD.value();

    // Source-major 4x4 matrix, mMatrixFlat[src*4 + dst]. User-facing name
    // MatrixIJ means "I modulates J" (first letter source, second letter
    // destination), matching AlembicRef's phaseIJ / "ItoJ" convention.
    // Diagonals are pre-scaled by kFbScale here so the per-sample matrix
    // sum path has no src==dst ternary.
    mMatrixFlat[0] = mMatrixAA.value() * kFbScale;
    mMatrixFlat[1] = mMatrixAB.value();
    mMatrixFlat[2] = mMatrixAC.value();
    mMatrixFlat[3] = mMatrixAD.value();
    mMatrixFlat[4] = mMatrixBA.value();
    mMatrixFlat[5] = mMatrixBB.value() * kFbScale;
    mMatrixFlat[6] = mMatrixBC.value();
    mMatrixFlat[7] = mMatrixBD.value();
    mMatrixFlat[8] = mMatrixCA.value();
    mMatrixFlat[9] = mMatrixCB.value();
    mMatrixFlat[10] = mMatrixCC.value() * kFbScale;
    mMatrixFlat[11] = mMatrixCD.value();
    mMatrixFlat[12] = mMatrixDA.value();
    mMatrixFlat[13] = mMatrixDB.value();
    mMatrixFlat[14] = mMatrixDC.value();
    mMatrixFlat[15] = mMatrixDD.value() * kFbScale;

    // Block-rate NEON loads off class-member scratch arrays.
    const float32x4_t vRow0 = vld1q_f32(&mMatrixFlat[0]);
    const float32x4_t vRow1 = vld1q_f32(&mMatrixFlat[4]);
    const float32x4_t vRow2 = vld1q_f32(&mMatrixFlat[8]);
    const float32x4_t vRow3 = vld1q_f32(&mMatrixFlat[12]);
    const float32x4_t vDetuneSp = vmulq_n_f32(vld1q_f32(mDetuneFlat), sp);
    const float32x4_t vRatioF0Sp = vmulq_n_f32(vld1q_f32(mRatioFlat), f0 * sp);
    const float32x4_t vLevel = vld1q_f32(mLevelFlat);

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

      // Matrix * prevOut via column-broadcast FMA chain. No horizontal
      // reductions. Diagonal was pre-scaled by kFbScale at block setup,
      // so the inner path applies no per-sample ternary. See plan for
      // derivation: phaseArg[dst] = phase[dst] + sum_src M[src->dst] *
      // prev[src], expressed as prev[0]*vRow0 + prev[1]*vRow1 + ... with
      // vRow_src being the "source src broadcast to all 4 destinations."
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
