// AlembicVoice -- native 4-op phase-mod matrix voice (Alembic phase 2).
// A/B-equivalent to AlembicRef (Lua-graph 4x libcore.SineOscillator + 16
// GainBias matrix scalars). Implements the exact semantics of
// libcore.SineOscillator (simd_sin Cephes polynomial, 0.18 internal
// feedback scale, hard-reset sync on rising edge, phase wrap to [0,1))
// for each of the 4 operators, and consolidates the 4x4 matrix into a
// single per-sample sum over prevOut[] with 0.18 applied to the diagonal
// (self-feedback) terms.
//
// NEON-touched arrays are class members (not stack-locals, no alignas)
// per feedback_neon_intrinsics_drumvoice so gcc emits `vld1.32 [reg]`
// with no alignment hint on Cortex-A8. Phase 2b vectorizes the inner
// loop over these same members -- no header changes required.

#pragma once

#include <od/objects/Object.h>
#include <od/config.h>

namespace stolmine
{

  class AlembicVoice : public od::Object
  {
  public:
    AlembicVoice();
    virtual ~AlembicVoice();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mVOct{"V/Oct"};
    od::Inlet mSync{"Sync"};
    od::Outlet mOut{"Out"};

    od::Parameter mF0{"F0", 27.5f};
    od::Parameter mGlobalLevel{"Level", 0.5f};

    od::Parameter mRatioA{"RatioA", 1.0f};
    od::Parameter mRatioB{"RatioB", 1.0f};
    od::Parameter mRatioC{"RatioC", 1.0f};
    od::Parameter mRatioD{"RatioD", 1.0f};

    od::Parameter mLevelA{"LevelA", 0.0f};
    od::Parameter mLevelB{"LevelB", 0.0f};
    od::Parameter mLevelC{"LevelC", 0.0f};
    od::Parameter mLevelD{"LevelD", 0.0f};

    od::Parameter mDetuneA{"DetuneA", 0.0f};
    od::Parameter mDetuneB{"DetuneB", 0.0f};
    od::Parameter mDetuneC{"DetuneC", 0.0f};
    od::Parameter mDetuneD{"DetuneD", 0.0f};

    // Source-major matrix[src][dst]. MatrixIJ drives op J's phase argument
    // from op I's previous output (cross-mod when I != J, self-feedback
    // with 0.18 internal scale when I == J). First letter = source,
    // second letter = destination, matching AlembicRef's phaseIJ /
    // "ItoJ" convention.
    od::Parameter mMatrixAA{"MatrixAA", 0.0f};
    od::Parameter mMatrixAB{"MatrixAB", 0.0f};
    od::Parameter mMatrixAC{"MatrixAC", 0.0f};
    od::Parameter mMatrixAD{"MatrixAD", 0.0f};
    od::Parameter mMatrixBA{"MatrixBA", 0.0f};
    od::Parameter mMatrixBB{"MatrixBB", 0.0f};
    od::Parameter mMatrixBC{"MatrixBC", 0.0f};
    od::Parameter mMatrixBD{"MatrixBD", 0.0f};
    od::Parameter mMatrixCA{"MatrixCA", 0.0f};
    od::Parameter mMatrixCB{"MatrixCB", 0.0f};
    od::Parameter mMatrixCC{"MatrixCC", 0.0f};
    od::Parameter mMatrixCD{"MatrixCD", 0.0f};
    od::Parameter mMatrixDA{"MatrixDA", 0.0f};
    od::Parameter mMatrixDB{"MatrixDB", 0.0f};
    od::Parameter mMatrixDC{"MatrixDC", 0.0f};
    od::Parameter mMatrixDD{"MatrixDD", 0.0f};
#endif

  private:
    // NEON working memory as class members. Heap-allocated via operator
    // new so Cortex-A8 emits `vld1.32 [reg]` (no alignment hint), which
    // tolerates arbitrary alignment. Stack-local placement would trap
    // via `:64` hint promotion under -O3 -ffast-math (see
    // feedback_neon_intrinsics_drumvoice memory). No alignas, no
    // __attribute__((aligned)) -- both over-promote the hint.
    float mPhaseBank[4];      // per-op phase accumulator, wrapped to [0,1)
    float mPrevOutBank[4];    // per-op last-sample output (feedback source)
    float mPhaseArgBank[4];   // packed phase+matrix args for simd_sin
    float mSineBank[4];       // simd_sin output unpacked

    bool mSyncWasHigh;
  };

} // namespace stolmine
