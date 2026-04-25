// AlembicSphereGraphic -- 3D Voronoi sphere viz for the Alembic scan ply.
// LITERAL line-for-line port of mods/catchall/SomSphereGraphic.h, with
// the architectural fix from planning/alchemy-voice.md "Path A":
//
//   - The full per-pixel rendered grayscale is stored in mGrayCache[]
//     and reused across frames as long as (rotBucketX, rotBucketY,
//     scanNode) is unchanged. Steady-state per-frame work is just a
//     cheap blit.
//   - When the cache key changes, refresh is time-sliced over
//     kRefreshFrames consecutive frames so no single frame does the
//     expensive full recompute (~524K dot products at per-pixel scale).
//
// All shell logic, gray formulas, edge thresholds, lift formulas,
// and constants come bit-for-bit from SomSphereGraphic. The only
// substitutions are:
//   - sinf/cosf -> lutCos/lutSin (per feedback_package_trig_lut)
//   - asinf/atan2f -> precomputed per-node target table at construction
//   - mpSom->getNodeX/Y/Z(n) -> mNodeX0/Y0/Z0[n] (Fibonacci computed
//     in the graphic; AlembicVoice doesn't carry node positions yet)
//   - mpSom->getNodeRichness(n) -> mpAlembic->getNodeBrightness(n) * 4.0f
//     as a Phase 4 placeholder; Phase 5 will swap getNodeBrightness
//     for real training-derived richness and the *4.0 falls away.

#pragma once

#include <od/graphics/Graphic.h>
#include <od/graphics/FrameBuffer.h>
#include <stdint.h>

namespace stolmine
{
  class AlembicVoice;

  class AlembicSphereGraphic : public od::Graphic
  {
  public:
    AlembicSphereGraphic(int left, int bottom, int width, int height);
    virtual ~AlembicSphereGraphic();

    void follow(AlembicVoice *p);

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb);

    // Worst-case dimensions; smaller graphics use a subset.
    static const int kMaxPixels = 128 * 64;

    // Rotation buckets. 64/axis = 0.098 rad granularity. Integrator slew
    // (~0.02 rad/frame at small dist) takes ~5 frames to cross a bucket;
    // large jumps cross immediately and the time-sliced refresh hides
    // the spike.
    static const int kRotBuckets = 64;

    // Refresh slice rate. h pixel rows refreshed over kRefreshFrames
    // consecutive frames after a cache invalidation.
    static const int kRefreshFrames = 8;
#endif

  private:
    AlembicVoice *mpAlembic;

    // Fibonacci sphere positions in canonical (un-rotated) frame; computed
    // once in the constructor.
    float mNodeX0[64];
    float mNodeY0[64];
    float mNodeZ0[64];

    // Per-node camera target angles (asin Y / atan2 X,Z), computed once
    // at construction so the draw path doesn't need asinf/atan2f.
    float mNodeTargetRotX[64];
    float mNodeTargetRotY[64];

    // Rotated node positions, refreshed each frame.
    float mNodeX[64];
    float mNodeY[64];
    float mNodeZ[64];

    // Per-node lift + scan-proximity. Class members (not stack locals)
    // per feedback_neon_intrinsics_drumvoice.
    float mLiftVal[64];
    float mScanBright[64];

    // Camera integrator (scan-following tumble).
    float mRotX;
    float mRotY;
    float mTargetRotX;
    float mTargetRotY;

    // Full per-pixel rendered grayscale cache. Survives across frames
    // until the cache key changes. Class member (not stack-local) per
    // feedback_neon_intrinsics_drumvoice.
    uint8_t mGrayCache[kMaxPixels];

    // Cache key. Sentinels (-1) force refresh on first draw.
    int mRotBucketX;
    int mRotBucketY;
    int mScanNodeCached;

    // Time-slice progress: number of rows refreshed since the last cache
    // invalidation. == mHeight means the cache is fresh.
    int mRefreshProgress;

#ifndef SWIGLUA
    void refreshCacheSlice(int rowStart, int rowEnd, float outerR2,
                           float invOuterR);
#endif
  };

} // namespace stolmine
