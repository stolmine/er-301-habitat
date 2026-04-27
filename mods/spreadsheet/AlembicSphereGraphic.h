// AlembicSphereGraphic -- 3D Voronoi sphere viz for the Alembic scan ply.
// LITERAL line-for-line port of mods/catchall/SomSphereGraphic.h with two
// architectural changes per planning/alchemy-voice.md "Path A":
//
//   - Per-pixel partition decision (kind + cell index) is cached and
//     keyed on rotation buckets only. The expensive nearestSeed /
//     twoNearestSeeds work runs once per bucket transition, time-sliced
//     across kRefreshFrames frames.
//
//   - Per-frame the rendering loop reads kind+cell from the cache and
//     computes gray fresh using current scanBright[cell] and per-pixel
//     hz. This keeps the visual responsive to scan motion without
//     invalidating the cache (scanBright changes every frame as the
//     focused node and chain distance shift, but the partition stays
//     valid while rotation is in the same bucket).
//
// Aesthetic decisions are bit-identical to SomSphereGraphic:
//   - Two-shell logic (outer = lifted shards top face, inner = base
//     surface or side wall)
//   - liftVal[n] = richness/(richness+2) (Phase 4 placeholder uses
//     getNodeBrightness * 4 as faux-richness)
//   - scanBright[n] from 3D distance-from-scan in canonical frame
//   - twoNearestSeeds for inner-shell edge detection
//   - Top face / side wall / base surface / base edge gray formulas
//     (5+sb*8, 3+sb*3, 4+sb*6, 1+sb*7), all scaled by hz depth
//   - 64-step sphere outline ring at the equator
//
// Substitutions:
//   - sinf/cosf -> lutCos/lutSin (per feedback_package_trig_lut)
//   - asinf/atan2f precomputed at construction
//   - mpSom getter calls -> AlembicVoice equivalents

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

    static const int kMaxPixels = 128 * 64;

    // Pixel kinds (stored in mPixelKind). Match SomSphereGraphic's branch
    // structure exactly; render path uses the corresponding gray formula.
    static const uint8_t kKindEmpty       = 0;
    static const uint8_t kKindTopFace     = 1;
    static const uint8_t kKindSideWall    = 2;
    static const uint8_t kKindBaseSurface = 3;
    static const uint8_t kKindBaseEdge    = 4;
    static const uint8_t kKindVoid        = 5;

    static const int kRotBuckets = 64;
    static const int kRefreshFrames = 8;
#endif

  private:
    AlembicVoice *mpAlembic;

    // Fibonacci sphere positions (canonical frame), computed once at
    // construction.
    float mNodeX0[64];
    float mNodeY0[64];
    float mNodeZ0[64];

    // Per-node camera target angles, computed once at construction so
    // the draw path has no asinf/atan2f.
    float mNodeTargetRotX[64];
    float mNodeTargetRotY[64];

    // Rotated node positions, refreshed each frame.
    float mNodeX[64];
    float mNodeY[64];
    float mNodeZ[64];

    // Per-node lift + scan-proximity. Class members per
    // feedback_neon_intrinsics_drumvoice.
    float mLiftVal[64];
    float mScanBright[64];

    // Camera integrator state.
    float mRotX;
    float mRotY;
    float mTargetRotX;
    float mTargetRotY;

    // Cached outer-shell radius squared (used during render for top-face
    // hz computation; kept consistent with whatever value was active when
    // the cache was last refreshed). In Phase 4 placeholder this is
    // stable; Phase 5 will need cache invalidation if maxLift changes.
    float mCachedOuterR;
    float mCachedOuterR2;

    // Per-pixel partition cache. mPixelKind[idx] is the render decision;
    // mPixelCell[idx] is which node won the Voronoi at that pixel. Cache
    // is invalidated only when rotation bucket crosses; per-frame render
    // uses these + current scanBright to paint.
    uint8_t mPixelKind[kMaxPixels];
    uint8_t mPixelCell[kMaxPixels];

    // Cache key. Sentinels (-1) force refresh on first draw.
    int mRotBucketX;
    int mRotBucketY;

    // Time-slice progress: rows refreshed since the last cache invalidation.
    int mRefreshProgress;

#ifndef SWIGLUA
    void refreshCacheSlice(int rowStart, int rowEnd, float outerR2,
                           float invOuterR);
#endif
  };

} // namespace stolmine
