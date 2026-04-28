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
#include <stdint.h>

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

    // Phase 5d-1.5 reagent controls. Reagent scan is a SECOND scan
    // position, independent of mScanPos. The wavetable LUT shape and
    // its trained row[28] blend amount come from K=4 nodes around
    // mReagentScan, while the PM voice (ratios/levels/detunes/matrix)
    // continues to use mScanPos's neighborhood. This lets the user
    // pick a tonal node region as source and a chaotic node region as
    // shaper (or any combination). mReagent is the global amount
    // multiplier on the trained blend; default 0 = clean PM at boot.
    od::Parameter mReagentScan{"ReagentScan", 0.0f};
    od::Parameter mReagent{"Reagent", 0.0f};

    // Phase 5d-2 filter pair user-bias hooks. Trained values live in
    // row[29..34]; these Parameters are the Phase 7 fade hooks.
    // Cutoffs / Q / topoMix / bpLpBlend / drive normalized [0,1] per
    // Som's convention (mapped to Hz / Q / etc at process time).
    od::Parameter mFilterCutoff1{"FilterCutoff1", 0.6f};
    od::Parameter mFilterCutoff2{"FilterCutoff2", 0.6f};
    od::Parameter mFilterQ{"FilterQ", 0.0f};
    od::Parameter mTopoMix{"TopoMix", 0.0f};
    od::Parameter mBpLpBlend{"BpLpBlend", 1.0f};
    od::Parameter mDrive{"Drive", 0.2f};

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

    // Phase 5d-1: per-node transfer-function LUT. Built from sample
    // windows at the trained picks (DC-removed, RMS-normalized, soft-
    // clipped, folded around the absolute peak with per-node even/odd
    // symmetry chosen from features). 256-entry resolution sourced
    // from a 256-sample multi-cycle window so the curve has multiple
    // bends (wavefolder-style character) rather than the gentle sub-
    // cycle slice of the earlier 32-entry version. Audio-rate softSat
    // -> LUT lookup -> K=4 frame crossfade. 64 frames * 256 entries *
    // 4 bytes = 64 KB.
    float mWavetableLUT[64][256];

    // Block-rate K-blended wavetable blend amount (the row[28] sum).
    // process()'s per-sample shaper crossfades identity ↔ full LUT
    // by this scalar.
    float mWavetableBlend;

    // Phase 5d-2 TPT SVF state (lifted from Som's filter pair). Two
    // filters per voice; integrators ic1 (BP) and ic2 (LP) per filter.
    // Class members per feedback_neon_intrinsics_drumvoice (heap, not
    // stack-local). Initialized to 0 in constructor + setSample(null).
    float mSvfIc1[2];
    float mSvfIc2[2];

    // Block-rate K-blended filter base scratch (cutoff1, cutoff2, Q,
    // topoMix, bpLpBlend, drive in [0,1]). Mapped to Hz / Q / etc at
    // process time.
    float mFilterFlat[6];

    // Phase 5d-3 routing matrix. Per-pick training picks 8 (src, dst)
    // lanes; lane attens live in row[35..42] of the preset table.
    // src indices in [0..9], dst indices in [0..5]:
    //   src 0..3 = PMM op outputs A, B, C, D (mSineBank)
    //   src 4..6 = Filter1 LP, BP, HP (from svf state + filter input)
    //   src 7..9 = Filter2 LP, BP, HP
    //   dst 0..1 = Filter1 cutoff verso, inverso (subtract -> freqMod)
    //   dst 2..3 = Filter2 cutoff verso, inverso
    //   dst 4    = Filter1 input addAdd
    //   dst 5    = Filter2 input addAdd
    //
    // Hard-cut routing: process() picks the SINGLE nearest preset slot
    // to mScanPos and uses its 8 lanes directly -- no K-blend across
    // slot boundaries. Routing topology snaps at boundaries (per user
    // taste -- matches the click/pop character the unit is going for).
    uint8_t mLaneSrc[64][8];
    uint8_t mLaneDst[64][8];

    // Block-rate active-edge scratch. Up to 8 lanes (1 slot, hard cut)
    // -- zero-atten lanes filtered out at block setup so the per-sample
    // edge loop only processes useful contributions.
    struct AlembicEdge { uint8_t src; uint8_t dst; float atten; };
    AlembicEdge mActiveEdges[8];
    int mActiveEdgeCount;

    // Per-sample routing sources / destination scratch as CLASS MEMBERS
    // (not stack-local) per feedback_neon_intrinsics_drumvoice. Stack-
    // local NEON-touched arrays under -O3 -ffast-math get gcc auto-vec
    // with `:64` alignment hints which trap on Cortex-A8. Holding these
    // on the heap (via the AlembicVoice instance) keeps gcc from
    // promoting hints on access patterns. Loops over them are scalar
    // (no vectorize attribute on process() but the small 8-edge loop
    // is unlikely to auto-vec anyway).
    float mRoutingSources[10];
    float mRoutingDst[6];

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
