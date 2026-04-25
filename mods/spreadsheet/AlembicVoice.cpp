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

    const float ratio[4] = {
        mRatioA.value(), mRatioB.value(), mRatioC.value(), mRatioD.value()};
    const float level[4] = {
        mLevelA.value(), mLevelB.value(), mLevelC.value(), mLevelD.value()};
    const float detune[4] = {
        mDetuneA.value(), mDetuneB.value(), mDetuneC.value(), mDetuneD.value()};

    // Source-major 4x4 matrix, matrix[src*4 + dst]. The user-facing name
    // MatrixIJ means "I modulates J" (first letter source, second letter
    // destination), matching AlembicRef's phaseIJ / "ItoJ" convention.
    // Diagonal entries (src == dst) are scaled by kFbScale at accumulation
    // time to match the reference semantics; everything else is 1x
    // cross-mod.
    const float matrix[16] = {
        mMatrixAA.value(), mMatrixAB.value(), mMatrixAC.value(), mMatrixAD.value(),
        mMatrixBA.value(), mMatrixBB.value(), mMatrixBC.value(), mMatrixBD.value(),
        mMatrixCA.value(), mMatrixCB.value(), mMatrixCC.value(), mMatrixCD.value(),
        mMatrixDA.value(), mMatrixDB.value(), mMatrixDC.value(), mMatrixDD.value()};

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // V/Oct: clamp to [-1,1] and apply FULLSCALE_IN_VOLTS*ln2 scaling,
      // matching SineOscillator.cpp:57-59. Scalar expf here; Phase 2b
      // uses the 4-wide simd_exp inline with the NEON phase advance.
      float q = vOct[i];
      if (q > 1.0f) q = 1.0f;
      if (q < -1.0f) q = -1.0f;
      const float ex = expf(q * glog2);

      // Per-op phase advance. freq = f0 * ratio * exp(V/Oct) + detune.
      // detune is added linearly (Hz), consistent with AlembicRef's
      // per-op tune GainBias summed into tuneSum alongside ratioX*f0.
      for (int op = 0; op < 4; op++)
      {
        const float freq = f0 * ratio[op] * ex + detune[op];
        mPhaseBank[op] += sp * freq;
      }

      // Sync: hard reset all 4 phases to 0 on rising edge. Threshold
      // > 0.5f per feedback_comparator_gate_threshold -- loose 0.0
      // thresholds trip on uninitialized fuzz / DC residue and
      // false-fire the reset handler.
      const bool high = sync[i] > 0.5f;
      if (high && !mSyncWasHigh)
      {
        mPhaseBank[0] = 0.0f;
        mPhaseBank[1] = 0.0f;
        mPhaseBank[2] = 0.0f;
        mPhaseBank[3] = 0.0f;
      }
      mSyncWasHigh = high;

      // Wrap phase to [0,1). Truncate-toward-zero cast is floor() for
      // the non-negative phase regime we operate in.
      for (int op = 0; op < 4; op++)
      {
        mPhaseBank[op] -= (int)mPhaseBank[op];
      }

      // Consolidated matrix sum. For destination dst, phaseArg[dst] is
      // its own accumulated phase plus the weighted sum of all 4
      // operators' previous outputs. Diagonal (src==dst) picks up
      // kFbScale to match SineOscillator's internal feedback handling;
      // off-diagonal cross-mod is 1x (matches AlembicRef's Phase-inlet
      // path). The (src == dst) predicate is compile-time-known after
      // loop unrolling so this produces no runtime branches per
      // feedback_runtime_branched_dsp_dispatch.
      for (int dst = 0; dst < 4; dst++)
      {
        float acc = mPhaseBank[dst];
        for (int src = 0; src < 4; src++)
        {
          const float scale = (src == dst) ? kFbScale : 1.0f;
          acc += matrix[src * 4 + dst] * mPrevOutBank[src] * scale;
        }
        mPhaseArgBank[dst] = acc;
      }

      // Vectorized sine over 4 packed args (one simd_sin call per
      // sample). mPhaseArgBank is a class member so the vld1q emits
      // the unaligned-safe `vld1.32 [reg]` form on Cortex-A8 under
      // -O3 -ffast-math.
      float32x4_t vArg = vld1q_f32(mPhaseArgBank);
      float32x4_t vSine = simd_sin(vmulq_n_f32(vArg, kTwoPi));
      vst1q_f32(mSineBank, vSine);

      // Output: per-op level-weighted sum, scaled by global level.
      const float sum = mSineBank[0] * level[0] + mSineBank[1] * level[1] +
                        mSineBank[2] * level[2] + mSineBank[3] * level[3];
      out[i] = sum * lvl;

      // Advance prev-out for next sample's matrix/feedback computation.
      mPrevOutBank[0] = mSineBank[0];
      mPrevOutBank[1] = mSineBank[1];
      mPrevOutBank[2] = mSineBank[2];
      mPrevOutBank[3] = mSineBank[3];
    }
  }

} // namespace stolmine
