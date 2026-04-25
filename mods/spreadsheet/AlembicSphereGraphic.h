// AlembicSphereGraphic -- 3D Voronoi sphere viz for the Alembic scan ply.
// Path A from planning/alchemy-voice.md: preserves the alchemical-orb
// aesthetic (Fibonacci nodes on a sphere, scan-following tumble, depth
// shading) while adopting the Colmatage-style discipline that keeps
// hardware draw cost bounded:
//
//   - 4x4 tile rendering (32x16 = 512 tiles), not per-pixel.
//   - Voronoi partition cache keyed on (rotBucketX, rotBucketY, scanNode).
//   - Time-sliced partition refresh on cache key change (4 rows/frame).
//   - Pre-baked sin/cos LUT in the .cpp (no runtime sinf/cosf in draw
//     per feedback_package_trig_lut).
//
// Phase 4a: architecture + basic sphere (this commit). Phase 4b layers
// the lifted focused node, slow Z-roll drift, and scan-proximity
// brightness modulation.
//
// All NEON-loadable / per-frame arrays are class members per
// feedback_neon_intrinsics_drumvoice + feedback_neon_hint_surfaces.

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

    static const int kTileSize = 4;
    static const int kGridW = 32; // 128 / 4
    static const int kGridH = 16; // 64 / 4
    static const int kRotBuckets = 64;
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

    // Camera integrator (scan-following tumble).
    float mRotX;
    float mRotY;
    float mTargetRotX;
    float mTargetRotY;

    // Cache key for the Voronoi partition. Sentinels (-1) force refresh
    // on first draw.
    int mRotBucketX;
    int mRotBucketY;
    int mScanNodeCached;

    // Tile partition cache: for each tile, which node index covers its
    // center. uint8_t is enough (64 nodes < 256).
    uint8_t mPartition[kGridW * kGridH];

    // Time-slice progress. < kGridH means partition refresh in flight.
    int mRefreshProgress;

    // Slewed per-tile brightness, prevents hard transitions on partition
    // updates.
    float mTileBrightness[kGridW * kGridH];

#ifndef SWIGLUA
    void refreshPartitionSlice(int rowStart, int rowEnd);
#endif
  };

} // namespace stolmine
