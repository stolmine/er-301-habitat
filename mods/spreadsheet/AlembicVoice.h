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
#include <od/audio/Sample.h>
#include <od/config.h>

// Forward-declare pffft setup so AlembicVoice.h doesn't have to pull in
// pffft.h. The .cpp includes pffft.h for the actual API.
struct PFFFT_Setup;

namespace stolmine
{

  class AlembicVoice : public od::Object
  {
  public:
    AlembicVoice();
    virtual ~AlembicVoice();

    // Sample-pool slot. Lua passes sample.pSample (a od::Sample*); the
    // unit attaches/releases per the standard SDK lifecycle (see
    // od::Head::setSample for the canonical pattern). Phase 5a stores
    // the pointer; Phase 5b will trigger analyzeSample() here.
    void setSample(od::Sample *sample);
    od::Sample *getSample();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mVOct{"V/Oct"};
    od::Inlet mSync{"Sync"};
    od::Outlet mOut{"Out"};

    od::Parameter mF0{"F0", 27.5f};
    od::Parameter mGlobalLevel{"Level", 0.5f};

    // Phase 3 scan-driven preset playback. ScanPos walks the 64-slot
    // preset table; ScanK is the path-window width (number of bracketing
    // nodes blended per block, clamped to [2,6]).
    od::Parameter mScanPos{"ScanPos", 0.0f};
    od::Parameter mScanK{"ScanK", 4.0f};

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

    // Phase 5d-1 reagent (wavetable transfer-function blend). The per-
    // node row[28] is sample-derived; this Parameter is the user-bias
    // hook for Phase 7's Reagent global to scale it.
    od::Parameter mReagent{"Reagent", 0.0f};

    // Phase 4 viz hooks. Inline so the sphere's draw loop has no dispatch
    // overhead. getNodeBrightness is a Phase 3 placeholder (linear gradient
    // per slot index); Phase 5 will replace it with sample-trained richness.
    // Not const: od::Parameter::value() is non-const in this SDK.
    inline int getScanNode()
    {
      const float s = mScanPos.value() * 63.0f;
      int n = (int)(s + 0.5f);
      if (n < 0) n = 0;
      else if (n > 63) n = 63;
      return n;
    }
    // Fractional scan position in node-index space [0, 63]. Used by the
    // viz for smooth chain-distance falloff (rounded scanNode would
    // create stepped halos as scan crosses node boundaries).
    inline float getScanNodeFloat()
    {
      float s = mScanPos.value() * 63.0f;
      if (s < 0.0f) s = 0.0f;
      else if (s > 63.0f) s = 63.0f;
      return s;
    }
    inline float getNodeBrightness(int n) const
    {
      if (n < 0 || n > 63) return 0.0f;
      return 0.2f + 0.8f * ((float)n / 63.0f);
    }
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

    // Block-rate NEON scratch arrays. Per feedback_neon_intrinsics_drumvoice,
    // stack-local float[4] arrays promote `vld1.32 [reg :64]` hints under
    // -O3 -ffast-math and trap on Cortex-A8 misalignment. Holding the
    // matrix, ratio, detune, and level buffers as class members ensures
    // the block-rate vld1q emits the unaligned-safe no-hint form. Phase 3
    // also uses these as crossfader output buffers (the K-weighted preset
    // blend writes here at block setup; per-sample NEON loop reads them
    // unchanged).
    float mMatrixFlat[16];    // source-major, diagonal pre-scaled by 0.18
    float mRatioFlat[4];
    float mDetuneFlat[4];
    float mLevelFlat[4];

    // Per-instance preset table. Phase 3 placeholder gradient fills this
    // at construction; Phase 5 analyzeSample() overwrites with sample-
    // derived content. Per-instance (was file-scope static in Phases 3-4)
    // because each unit trains on its own sample.
    //
    // Slot layout (43 floats per row, finalized in Phase 5d):
    //   [0..3]   ratios          (5b)
    //   [4..7]   levels          (5b)
    //   [8..11]  detunes         (5b/5c, op-A drift at row[8])
    //   [12..27] matrix 4x4      (5b + 5c topology mask)
    //   [28]     wavetable blend (5d-1; was scalar reagent)
    //   [29..34] filter base     (5d-2: cutoff1, cutoff2, Q, topoMix,
    //                            bpLpBlend, drive)
    //   [35..42] lane attens     (5d-3: 8 routing slots)
    //
    // Phase 5d-1 pre-grows the row to 43 floats so 5d-2/5d-3 land
    // without further SWIG regen. ~11008 bytes.
    float mPresetTable[64][43];

    // Phase 5d-1: per-node 32-entry transfer-function LUT. Built from
    // 32-sample windows at the trained picks (DC-removed, peak-
    // normalized, cosine-edge-faded). Audio-rate softSat → LUT lookup
    // → K=4 frame crossfade replaces the planned scalar nonlinearity.
    // ~8192 bytes.
    float mWavetableLUT[64][32];

    // Block-rate K-blended wavetable blend amount (the row[28] sum).
    // process()'s per-sample shaper crossfades identity ↔ full LUT
    // by this scalar.
    float mWavetableBlend;

    // Sample-pool slot. Lifetime managed via attach/release pairs in
    // setSample. Phase 5b reads it once during analyzeSample().
    od::Sample *mpSample;

    bool mSyncWasHigh;

    // Phase 5b: offline feature-extraction kernel. Triggered from
    // setSample() when a non-null sample arrives; populates mPresetTable
    // from the sample's content. Synchronous on the calling thread
    // (200ms-1.2s wall-clock per planning doc). All scratch is heap-
    // allocated (never stack); helpers are noinline + no-tree-vectorize
    // per feedback_neon_hint_surfaces.
    void analyzeSample();

    // pffft state for offline spectral analysis. Lazily allocated on
    // first analyzeSample() call and reused thereafter; freed in
    // destructor. Buffers must be 16-byte aligned (pffft_aligned_malloc).
    PFFFT_Setup *mFftSetup;
    float *mFftIn;
    float *mFftOut;
    float *mFftWork;
    // Hann window precomputed at construction (256 entries for 256-pt FFT).
    float mHannWindow[256];
    // Magnitude spectrum scratch + previous-frame buffer for flux.
    // Class members per feedback_neon_intrinsics_drumvoice; loops over
    // these are scalar (noinline + no-tree-vectorize).
    float mScratchMag[128];
    float mPrevMag[128];
  };

} // namespace stolmine
