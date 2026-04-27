// AlembicSphereGraphic implementation -- partition-cache port of
// SomSphereGraphic. See header for design notes.
//
// refreshCacheSlice() is a literal port of SomSphere::draw() lines
// 165-249, computing the kind+cell per pixel. The per-frame render
// loop applies SomSphere's gray formulas (lines 188-196 / 218-220 /
// 232-237) with cached kind+cell + current scanBright + per-pixel hz.

#include "AlembicSphereGraphic.h"
#include "AlembicVoice.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace stolmine
{
  // ---- 72-entry sin/cos LUT ---------------------------------------------
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

  // ---- Constructor + helpers --------------------------------------------
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void fillFibonacciTables(
      float (&x0)[64], float (&y0)[64], float (&z0)[64],
      float (&trX)[64], float (&trY)[64])
  {
    const float kGoldenAngle = (float)M_PI * (3.0f - 2.2360679775f);
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
        mCachedOuterR(1.0f), mCachedOuterR2(1.0f),
        mRotBucketX(-1), mRotBucketY(-1),
        mRefreshProgress(0)
  {
    fillFibonacciTables(mNodeX0, mNodeY0, mNodeZ0,
                        mNodeTargetRotX, mNodeTargetRotY);
    memset(mNodeX, 0, sizeof(mNodeX));
    memset(mNodeY, 0, sizeof(mNodeY));
    memset(mNodeZ, 0, sizeof(mNodeZ));
    memset(mLiftVal, 0, sizeof(mLiftVal));
    memset(mScanBright, 0, sizeof(mScanBright));
    memset(mPixelKind, 0, sizeof(mPixelKind));
    memset(mPixelCell, 0, sizeof(mPixelCell));
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

  // ---- refreshCacheSlice -----------------------------------------------
  // Literal port of SomSphere::draw lines 165-249, but stores (kind, cell)
  // into the cache instead of gray. Per-pixel decisions follow the same
  // shell tests, edge thresholds, and lift comparisons.
  __attribute__((optimize("no-tree-vectorize")))
  void AlembicSphereGraphic::refreshCacheSlice(int rowStart, int rowEnd,
                                               float outerR2, float invOuterR)
  {
    const int w = mWidth, h = mHeight;
    const int minDim = w < h ? w : h;
    const float screenR = (float)minDim * 0.34f;
    const float liftAmount = 0.35f;
    const float cx = (float)w * 0.5f;
    const float cy = (float)h * 0.5f;
    const float invScreenR = 1.0f / screenR;
    const float outerR = 1.0f / invOuterR;

    for (int py = rowStart; py < rowEnd; py++)
    {
      for (int px = 0; px < w; px++)
      {
        const int idx = py * w + px;
        const float nx = ((float)px - cx) * invScreenR;
        const float ny = ((float)py - cy) * invScreenR;
        const float d2 = nx * nx + ny * ny;

        uint8_t kind = kKindEmpty;
        uint8_t cell = 0;
        bool drawn = false;

        // OUTER SHELL: lifted shards top face (SomSphere lines 177-198).
        if (d2 < outerR2)
        {
          const float zOuter = sqrtf(outerR2 - d2);
          const float hx = nx * invOuterR;
          const float hy = ny * invOuterR;
          const float hz = zOuter * invOuterR;
          (void)hz;

          int bestIdx = 0;
          float bestDot = -2.0f;
          for (int n = 0; n < 64; n++)
          {
            if (mNodeZ[n] < -0.1f) continue;
            const float d = hx * mNodeX[n] + hy * mNodeY[n] + hz * mNodeZ[n];
            if (d > bestDot) { bestDot = d; bestIdx = n; }
          }
          const float cellR = 1.0f + mLiftVal[bestIdx] * liftAmount;
          if (cellR > 0.95f * outerR && mLiftVal[bestIdx] > 0.1f &&
              d2 < cellR * cellR)
          {
            kind = kKindTopFace;
            cell = (uint8_t)bestIdx;
            drawn = true;
          }
        }

        // INNER SHELL: base surface, edge, side wall, void
        // (SomSphere lines 201-241).
        if (!drawn && d2 < 1.0f)
        {
          const float hz = sqrtf(1.0f - d2);
          const float hx = nx;
          const float hy = ny;
          (void)hz;

          int bestIdx = 0;
          int second = 0;
          float bestDot = -2.0f;
          float secondDot = -2.0f;
          for (int n = 0; n < 64; n++)
          {
            if (mNodeZ[n] < -0.1f) continue;
            const float d = hx * mNodeX[n] + hy * mNodeY[n] + hz * mNodeZ[n];
            if (d > bestDot)
            {
              secondDot = bestDot;
              second = bestIdx;
              bestDot = d;
              bestIdx = n;
            }
            else if (d > secondDot)
            {
              secondDot = d;
              second = n;
            }
          }
          (void)second;

          if (mLiftVal[bestIdx] > 0.1f)
          {
            const float edgeRatio = secondDot / (bestDot + 0.001f);
            if (edgeRatio > 0.92f)
            {
              kind = kKindSideWall;
              cell = (uint8_t)bestIdx;
            }
            else
            {
              kind = kKindVoid; // lifted cell, away from boundary
              cell = (uint8_t)bestIdx;
            }
          }
          else
          {
            const float edgeRatio = secondDot / (bestDot + 0.001f);
            const bool isEdge = edgeRatio > 0.98f;
            cell = (uint8_t)bestIdx;
            kind = isEdge ? kKindBaseEdge : kKindBaseSurface;
          }
        }

        mPixelKind[idx] = kind;
        mPixelCell[idx] = cell;
      }
    }
  }

  // ---- Per-frame draw ---------------------------------------------------
  __attribute__((optimize("no-tree-vectorize")))
  void AlembicSphereGraphic::draw(od::FrameBuffer &fb)
  {
    if (!mpAlembic) return;

    const int w = mWidth, h = mHeight;
    const int left = mWorldLeft, bot = mWorldBottom;
    const int scanNode = mpAlembic->getScanNode();

    // Camera target from precomputed table.
    mTargetRotX = mNodeTargetRotX[scanNode];
    mTargetRotY = mNodeTargetRotY[scanNode];

    // Integrator slew (SomSphere lines 92-99 verbatim).
    {
      float dx = mTargetRotX - mRotX;
      float dy = mTargetRotY - mRotY;
      if (dy > (float)M_PI) dy -= 2.0f * (float)M_PI;
      if (dy < -(float)M_PI) dy += 2.0f * (float)M_PI;
      const float dist = sqrtf(dx * dx + dy * dy);
      mRotX += dx * (0.02f + dist * 0.12f);
      mRotY += dy * (0.02f + dist * 0.12f);
    }

    const float cosRx = lutCos(mRotX);
    const float sinRx = lutSin(mRotX);
    const float cosRy = lutCos(mRotY);
    const float sinRy = lutSin(mRotY);

    // Visual constants (SomSphere lines 104-109).
    const int minDim = w < h ? w : h;
    const float screenR = (float)minDim * 0.34f;
    const float liftAmount = 0.35f;
    const float cx = (float)w * 0.5f;
    const float cy = (float)h * 0.5f;
    const float nbrRadius = 0.5f;
    const float invScreenR = 1.0f / screenR;

    // Per-node liftVal + scanBright (SomSphere lines 116-130 verbatim,
    // with class-member arrays instead of stack-locals).
    const float scanSX = mNodeX0[scanNode];
    const float scanSY = mNodeY0[scanNode];
    const float scanSZ = mNodeZ0[scanNode];
    float maxLift = 0.0f;
    for (int n = 0; n < 64; n++)
    {
      const float r = mpAlembic->getNodeBrightness(n) * 4.0f;
      mLiftVal[n] = r / (r + 2.0f);
      if (mLiftVal[n] > maxLift) maxLift = mLiftVal[n];

      const float dx = mNodeX0[n] - scanSX;
      const float dy = mNodeY0[n] - scanSY;
      const float dz = mNodeZ0[n] - scanSZ;
      const float sd = sqrtf(dx * dx + dy * dy + dz * dz);
      const float pb = 1.0f - sd / nbrRadius;
      mScanBright[n] = (pb > 0.0f) ? pb * pb : 0.0f;
    }

    // Rotate all 64 seeds (SomSphere lines 132-144 verbatim).
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

    const float outerR = 1.0f + maxLift * liftAmount;
    const float outerR2 = outerR * outerR;
    const float invOuterR = 1.0f / outerR;

    // Cache key: rotation buckets only. scanNode does NOT invalidate
    // the cache because per-pixel brightness is recomputed each frame
    // from current scanBright[cell].
    const float bucketScale = (float)kRotBuckets / (2.0f * (float)M_PI);
    const float bias = (float)kRotBuckets * 1000.0f;
    int newBucketX = (int)(mRotX * bucketScale + bias) % kRotBuckets;
    int newBucketY = (int)(mRotY * bucketScale + bias) % kRotBuckets;
    if (newBucketX != mRotBucketX || newBucketY != mRotBucketY)
    {
      mRotBucketX = newBucketX;
      mRotBucketY = newBucketY;
      mRefreshProgress = 0;
      // Stash outerR/outerR2 used for this refresh so the per-frame
      // render's top-face hz computation matches the cache's decisions.
      mCachedOuterR = outerR;
      mCachedOuterR2 = outerR2;
    }

    // Time-sliced refresh on cache miss.
    if (mRefreshProgress < h)
    {
      int rowsPerFrame = h / kRefreshFrames;
      if (rowsPerFrame < 1) rowsPerFrame = 1;
      int rowEnd = mRefreshProgress + rowsPerFrame;
      if (rowEnd > h) rowEnd = h;
      refreshCacheSlice(mRefreshProgress, rowEnd, mCachedOuterR2,
                        1.0f / mCachedOuterR);
      mRefreshProgress = rowEnd;
    }

    // ---- Per-pixel render ----
    // Read kind+cell from cache; compute current gray using SomSphere's
    // exact formulas (lines 188-196 / 218-220 / 232-237) with current
    // scanBright[cell] + per-pixel hz from sample-point geometry.
    const float invCachedOuterR = 1.0f / mCachedOuterR;
    for (int py = 0; py < h; py++)
    {
      for (int px = 0; px < w; px++)
      {
        const int idx = py * w + px;
        const uint8_t kind = mPixelKind[idx];
        if (kind == kKindEmpty || kind == kKindVoid) continue;

        const int cell = (int)mPixelCell[idx];
        const float sb = mScanBright[cell];

        // Recompute per-pixel d2 + hz. Cheap (~6 ops/pixel) and avoids
        // a 32KB precomputed table.
        const float nx = ((float)px - cx) * invScreenR;
        const float ny = ((float)py - cy) * invScreenR;
        const float d2 = nx * nx + ny * ny;

        int gray = 0;
        if (kind == kKindTopFace)
        {
          // SomSphere lines 188-196.
          float zo2 = mCachedOuterR2 - d2;
          if (zo2 < 0.0f) zo2 = 0.0f;
          const float hz = sqrtf(zo2) * invCachedOuterR;
          gray = 5 + (int)(sb * 8.0f);
          gray = (int)((float)gray * (0.3f + 0.7f * hz));
          if (gray > 13) gray = 13;
          if (gray < 3) gray = 3;
        }
        else if (kind == kKindSideWall)
        {
          // SomSphere lines 218-221.
          float zi2 = 1.0f - d2;
          if (zi2 < 0.0f) zi2 = 0.0f;
          const float hz = sqrtf(zi2);
          gray = 3 + (int)(sb * 3.0f);
          gray = (int)((float)gray * (0.2f + 0.5f * hz));
          if (gray < 2) gray = 2;
          if (gray > 13) gray = 13;
        }
        else if (kind == kKindBaseEdge)
        {
          // SomSphere lines 233 + 237.
          float zi2 = 1.0f - d2;
          if (zi2 < 0.0f) zi2 = 0.0f;
          const float hz = sqrtf(zi2);
          gray = 4 + (int)(sb * 6.0f);
          gray = (int)((float)gray * (0.3f + 0.7f * hz));
          if (gray > 13) gray = 13;
        }
        else if (kind == kKindBaseSurface)
        {
          // SomSphere lines 235 + 237.
          float zi2 = 1.0f - d2;
          if (zi2 < 0.0f) zi2 = 0.0f;
          const float hz = sqrtf(zi2);
          gray = 1 + (int)(sb * 7.0f);
          gray = (int)((float)gray * (0.3f + 0.7f * hz));
          if (gray > 13) gray = 13;
        }

        if (gray > 0)
          fb.pixel(gray, left + px, bot + py);
      }
    }

    // Sphere outline ring at the equator (SomSphere lines 252-266).
    {
      const int steps = 64;
      int prevOx = -1, prevOy = -1;
      for (int i = 0; i <= steps; i++)
      {
        const float a = (float)(i % steps) * (2.0f * (float)M_PI / (float)steps);
        const int ox = left + (int)(cx + lutCos(a) * screenR);
        const int oy = bot + (int)(cy + lutSin(a) * screenR);
        if (prevOx >= 0 &&
            ox >= left && ox < left + w && oy >= bot && oy < bot + h &&
            prevOx >= left && prevOx < left + w && prevOy >= bot && prevOy < bot + h)
        {
          fb.line(GRAY3, prevOx, prevOy, ox, oy);
        }
        prevOx = ox;
        prevOy = oy;
      }
    }

    (void)nbrRadius;
  }

} // namespace stolmine
