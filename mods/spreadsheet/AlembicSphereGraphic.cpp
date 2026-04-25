// AlembicSphereGraphic implementation -- see header for design notes.

#include "AlembicSphereGraphic.h"
#include "AlembicVoice.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace stolmine
{
  // ---- LUT-based trig (replaces sinf/cosf in the draw path) --------------
  // Local copy of FilterResponseGraphic.h's pattern; static at file scope so
  // the linker doesn't conflict with that other .o. 72-entry LUT spans 2*pi
  // with anchor at -pi/2 so kLutCos[0] = cos(-pi/2) = 0.
  static const float kLutCos[72] = {
      +0.00000000f, +0.08715574f, +0.17364818f, +0.25881905f, +0.34202014f,
      +0.42261826f, +0.50000000f, +0.57357644f, +0.64278761f, +0.70710678f,
      +0.76604444f, +0.81915204f, +0.86602540f, +0.90630779f, +0.93969262f,
      +0.96592583f, +0.98480775f, +0.99619470f, +1.00000000f, +0.99619470f,
      +0.98480775f, +0.96592583f, +0.93969262f, +0.90630779f, +0.86602540f,
      +0.81915204f, +0.76604444f, +0.70710678f, +0.64278761f, +0.57357644f,
      +0.50000000f, +0.42261826f, +0.34202014f, +0.25881905f, +0.17364818f,
      +0.08715574f, +0.00000000f, -0.08715574f, -0.17364818f, -0.25881905f,
      -0.34202014f, -0.42261826f, -0.50000000f, -0.57357644f, -0.64278761f,
      -0.70710678f, -0.76604444f, -0.81915204f, -0.86602540f, -0.90630779f,
      -0.93969262f, -0.96592583f, -0.98480775f, -0.99619470f, -1.00000000f,
      -0.99619470f, -0.98480775f, -0.96592583f, -0.93969262f, -0.90630779f,
      -0.86602540f, -0.81915204f, -0.76604444f, -0.70710678f, -0.64278761f,
      -0.57357644f, -0.50000000f, -0.42261826f, -0.34202014f, -0.25881905f,
      -0.17364818f, -0.08715574f};
  static const float kLutSin[72] = {
      -1.00000000f, -0.99619470f, -0.98480775f, -0.96592583f, -0.93969262f,
      -0.90630779f, -0.86602540f, -0.81915204f, -0.76604444f, -0.70710678f,
      -0.64278761f, -0.57357644f, -0.50000000f, -0.42261826f, -0.34202014f,
      -0.25881905f, -0.17364818f, -0.08715574f, +0.00000000f, +0.08715574f,
      +0.17364818f, +0.25881905f, +0.34202014f, +0.42261826f, +0.50000000f,
      +0.57357644f, +0.64278761f, +0.70710678f, +0.76604444f, +0.81915204f,
      +0.86602540f, +0.90630779f, +0.93969262f, +0.96592583f, +0.98480775f,
      +0.99619470f, +1.00000000f, +0.99619470f, +0.98480775f, +0.96592583f,
      +0.93969262f, +0.90630779f, +0.86602540f, +0.81915204f, +0.76604444f,
      +0.70710678f, +0.64278761f, +0.57357644f, +0.50000000f, +0.42261826f,
      +0.34202014f, +0.25881905f, +0.17364818f, +0.08715574f, +0.00000000f,
      -0.08715574f, -0.17364818f, -0.25881905f, -0.34202014f, -0.42261826f,
      -0.50000000f, -0.57357644f, -0.64278761f, -0.70710678f, -0.76604444f,
      -0.81915204f, -0.86602540f, -0.90630779f, -0.93969262f, -0.96592583f,
      -0.98480775f, -0.99619470f};

  // Bias-then-cast-then-modulo lookup. Bias by 72*1000 periods so negative
  // angles truncate safely without floorf. Linear interpolation between
  // adjacent table entries.
  static inline float lutCos(float rad)
  {
    const float scale = 72.0f / (2.0f * (float)M_PI);
    const float bias = (float)M_PI * 0.5f * scale + 72.0f * 1000.0f;
    float t = rad * scale + bias;
    int ii = (int)t;
    float frac = t - (float)ii;
    int i = ii % 72;
    int next = (i + 1) % 72;
    return kLutCos[i] + (kLutCos[next] - kLutCos[i]) * frac;
  }

  static inline float lutSin(float rad)
  {
    const float scale = 72.0f / (2.0f * (float)M_PI);
    const float bias = (float)M_PI * 0.5f * scale + 72.0f * 1000.0f;
    float t = rad * scale + bias;
    int ii = (int)t;
    float frac = t - (float)ii;
    int i = ii % 72;
    int next = (i + 1) % 72;
    return kLutSin[i] + (kLutSin[next] - kLutSin[i]) * frac;
  }

  // ---- Constructor: precompute Fibonacci nodes + camera target table -----
  // No tree-vec on this initializer. The 64-element loops are exactly the
  // shape that GCC auto-vectorizes (linear array writes from scalar math),
  // which would emit `:64` aligned NEON ops -- the construction-time trap
  // surface from feedback_neon_hint_surfaces. noinline + no-tree-vectorize
  // forces a scalar build.
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void fillFibonacciTables(
      float (&x0)[64], float (&y0)[64], float (&z0)[64],
      float (&trX)[64], float (&trY)[64])
  {
    const float kGoldenAngle = (float)M_PI * (3.0f - 2.2360679775f); // pi*(3-sqrt(5))
    for (int n = 0; n < 64; n++)
    {
      const float y = 1.0f - 2.0f * ((float)n + 0.5f) / 64.0f;
      const float r2 = 1.0f - y * y;
      const float r = r2 > 0.0f ? sqrtf(r2) : 0.0f;
      const float theta = (float)n * kGoldenAngle;
      const float x = lutCos(theta) * r;
      const float z = lutSin(theta) * r;
      x0[n] = x;
      y0[n] = y;
      z0[n] = z;

      // Camera-target precomputation: per planning doc lines 154-156, with
      // SomSphereGraphic's small fixed offsets baked in for visual taste
      // (-0.3 X tilt, +0.25 Y bias). Construction-time only, so asinf /
      // atan2f are safe (not in the package draw path).
      float yc = y;
      if (yc > 1.0f) yc = 1.0f;
      else if (yc < -1.0f) yc = -1.0f;
      trX[n] = asinf(yc) - 0.3f;
      trY[n] = atan2f(x, z) + 0.25f;
    }
  }

  AlembicSphereGraphic::AlembicSphereGraphic(int left, int bottom, int w, int h)
      : od::Graphic(left, bottom, w, h),
        mpAlembic(nullptr),
        mRotX(0.0f), mRotY(0.0f),
        mTargetRotX(0.0f), mTargetRotY(0.0f),
        mRotBucketX(-1), mRotBucketY(-1), mScanNodeCached(-1),
        mRefreshProgress(0)
  {
    fillFibonacciTables(mNodeX0, mNodeY0, mNodeZ0,
                        mNodeTargetRotX, mNodeTargetRotY);
    memset(mNodeX, 0, sizeof(mNodeX));
    memset(mNodeY, 0, sizeof(mNodeY));
    memset(mNodeZ, 0, sizeof(mNodeZ));
    memset(mPartition, 0, sizeof(mPartition));
    memset(mTileBrightness, 0, sizeof(mTileBrightness));
  }

  AlembicSphereGraphic::~AlembicSphereGraphic()
  {
    if (mpAlembic) mpAlembic->release();
  }

  void AlembicSphereGraphic::follow(AlembicVoice *p)
  {
    if (mpAlembic) mpAlembic->release();
    mpAlembic = p;
    if (mpAlembic) mpAlembic->attach();
  }

  // Refresh a horizontal stripe of the partition cache. Tile center is
  // projected onto the unit sphere; nearest-node by 3D dot product over
  // the post-rotation positions. Back-face cull (Z < -0.1) skips nodes
  // that are behind the sphere from the viewer's perspective.
  __attribute__((optimize("no-tree-vectorize")))
  void AlembicSphereGraphic::refreshPartitionSlice(int rowStart, int rowEnd)
  {
    const int w = mWidth, h = mHeight;
    const int minDim = w < h ? w : h;
    const float screenR = (float)minDim * 0.34f;
    const float cx = (float)w * 0.5f;
    const float cy = (float)h * 0.5f;
    const float invR = 1.0f / screenR;

    for (int ty = rowStart; ty < rowEnd; ty++)
    {
      for (int tx = 0; tx < kGridW; tx++)
      {
        const float pxC = (float)(tx * kTileSize) + (float)kTileSize * 0.5f;
        const float pyC = (float)(ty * kTileSize) + (float)kTileSize * 0.5f;
        const float nx = (pxC - cx) * invR;
        const float ny = (pyC - cy) * invR;
        const float d2 = nx * nx + ny * ny;

        uint8_t bestNode = 0;
        if (d2 < 1.0f)
        {
          const float hz = sqrtf(1.0f - d2);
          const float hx = nx;
          const float hy = ny;
          float bestDot = -2.0f;
          for (int n = 0; n < 64; n++)
          {
            if (mNodeZ[n] < -0.1f) continue; // back-face cull
            const float d = hx * mNodeX[n] + hy * mNodeY[n] + hz * mNodeZ[n];
            if (d > bestDot)
            {
              bestDot = d;
              bestNode = (uint8_t)n;
            }
          }
        }
        mPartition[ty * kGridW + tx] = bestNode;
      }
    }
  }

  // ---- Per-frame draw -----------------------------------------------------
  // Tree-vec disabled on the draw path too -- the per-tile loops over node
  // arrays would otherwise auto-vec with `:64` hints on .bss arrays.
  __attribute__((optimize("no-tree-vectorize")))
  void AlembicSphereGraphic::draw(od::FrameBuffer &fb)
  {
    if (!mpAlembic) return;

    const int w = mWidth, h = mHeight;
    const int left = mWorldLeft, bot = mWorldBottom;
    const int scanNode = mpAlembic->getScanNode();

    // 1. Camera target from precomputed per-node table.
    mTargetRotX = mNodeTargetRotX[scanNode];
    mTargetRotY = mNodeTargetRotY[scanNode];

    // 2. Integrator slew with momentum (planning doc lines 159-167).
    float dx = mTargetRotX - mRotX;
    float dy = mTargetRotY - mRotY;
    if (dy > (float)M_PI) dy -= 2.0f * (float)M_PI;
    if (dy < -(float)M_PI) dy += 2.0f * (float)M_PI;
    const float dist = sqrtf(dx * dx + dy * dy);
    const float slew = 0.02f + dist * 0.12f;
    mRotX += dx * slew;
    mRotY += dy * slew;

    // 3. Compute new rotation buckets. Bucket width = 2*pi / kRotBuckets.
    const float bucketScale = (float)kRotBuckets / (2.0f * (float)M_PI);
    const float bias = (float)kRotBuckets * 1000.0f;
    int newBucketX = (int)(mRotX * bucketScale + bias) % kRotBuckets;
    int newBucketY = (int)(mRotY * bucketScale + bias) % kRotBuckets;

    if (newBucketX != mRotBucketX || newBucketY != mRotBucketY ||
        scanNode != mScanNodeCached)
    {
      mRefreshProgress = 0;
      mRotBucketX = newBucketX;
      mRotBucketY = newBucketY;
      mScanNodeCached = scanNode;
    }

    // 4. Rotate all 64 nodes via LUT cos/sin (no runtime sinf/cosf).
    const float cosRx = lutCos(mRotX);
    const float sinRx = lutSin(mRotX);
    const float cosRy = lutCos(mRotY);
    const float sinRy = lutSin(mRotY);
    for (int n = 0; n < 64; n++)
    {
      const float x = mNodeX0[n];
      const float y = mNodeY0[n];
      const float z = mNodeZ0[n];
      const float rx = x * cosRy - z * sinRy;
      const float rz = x * sinRy + z * cosRy;
      mNodeX[n] = rx;
      mNodeY[n] = y * cosRx - rz * sinRx;
      mNodeZ[n] = y * sinRx + rz * cosRx;
    }

    // 5. Time-sliced partition refresh. 4 rows / 128 tiles per frame on
    //    cache miss; full grid refreshes over kGridH/4 = 4 frames.
    if (mRefreshProgress < kGridH)
    {
      const int rowsPerFrame = 4;
      int rowEnd = mRefreshProgress + rowsPerFrame;
      if (rowEnd > kGridH) rowEnd = kGridH;
      refreshPartitionSlice(mRefreshProgress, rowEnd);
      mRefreshProgress = rowEnd;
    }

    // 6. Tile render. Each tile picks its partition node, depth-shades from
    //    that node's post-rotation Z, slews target into mTileBrightness,
    //    fills the 4x4 tile.
    const int minDim = w < h ? w : h;
    const float screenR = (float)minDim * 0.34f;
    const float cx = (float)w * 0.5f;
    const float cy = (float)h * 0.5f;
    const float invR = 1.0f / screenR;

    for (int ty = 0; ty < kGridH; ty++)
    {
      for (int tx = 0; tx < kGridW; tx++)
      {
        const int tileIdx = ty * kGridW + tx;
        const int node = (int)mPartition[tileIdx];

        const float pxC = (float)(tx * kTileSize) + (float)kTileSize * 0.5f;
        const float pyC = (float)(ty * kTileSize) + (float)kTileSize * 0.5f;
        const float nx = (pxC - cx) * invR;
        const float ny = (pyC - cy) * invR;
        const float d2 = nx * nx + ny * ny;

        float target = 0.0f;
        if (d2 < 1.0f)
        {
          // Depth shading: post-rotation Z, mapped from [-1,1] to [0,1].
          // Front of sphere bright, back dim.
          const float depth = mNodeZ[node];
          const float depthScale = 0.5f + 0.5f * depth;
          const float brightness = mpAlembic->getNodeBrightness(node);
          target = brightness * depthScale;
        }
        // Slew per tile (one-pole, slow). Damps partition-flip transitions.
        mTileBrightness[tileIdx] += (target - mTileBrightness[tileIdx]) * 0.2f;

        // Map [0,1] brightness to ER-301 12-level grayscale; clamp.
        int gray = (int)(mTileBrightness[tileIdx] * 12.0f + 0.5f);
        if (gray < 0) gray = 0;
        if (gray > 12) gray = 12;
        if (gray > 0)
        {
          for (int dyP = 0; dyP < kTileSize; dyP++)
          {
            for (int dxP = 0; dxP < kTileSize; dxP++)
            {
              const int px = tx * kTileSize + dxP;
              const int py = ty * kTileSize + dyP;
              if (px < w && py < h)
                fb.pixel(gray, left + px, bot + py);
            }
          }
        }
      }
    }
  }

} // namespace stolmine
