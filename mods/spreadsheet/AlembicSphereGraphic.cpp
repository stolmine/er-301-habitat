// AlembicSphereGraphic implementation -- literal line-for-line port of
// SomSphereGraphic::draw(), wrapped in a rotation-bucket-keyed
// grayscale cache with time-sliced refresh.
//
// Per feedback_identical_means_identical: the per-pixel rendering logic
// inside refreshCacheSlice() is structurally identical to SomSphere's
// draw loop (lines 146-249). The only substitutions are sinf/cosf ->
// lutCos/lutSin, asinf/atan2f -> precomputed table, and the Som getter
// references -> AlembicVoice equivalents.

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
  // Local copy of FilterResponseGraphic.h's pattern; static at file scope.
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

  // ---- Fibonacci + per-node target precomputation -----------------------
  // noinline + no-tree-vectorize per feedback_neon_hint_surfaces -- the
  // 64-element loops would otherwise auto-vec into NEON ops with `:64`
  // alignment hints on the .bss/stack arrays, which trap on Cortex-A8 at
  // construction time.
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

      // Construction-time only -- safe to use math.h asin/atan2 here.
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
    memset(mLiftVal, 0, sizeof(mLiftVal));
    memset(mScanBright, 0, sizeof(mScanBright));
    memset(mGrayCache, 0, sizeof(mGrayCache));
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

  // ---- refreshCacheSlice: line-for-line Som per-pixel logic --------------
  // SomSphereGraphic::draw lines 165-249 ported directly. Walks rows
  // [rowStart, rowEnd), evaluates each pixel's outer/inner shell decision,
  // computes the Som gray formula, and stores into mGrayCache[idx].
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
        const float nx = ((float)px - cx) * invScreenR;
        const float ny = ((float)py - cy) * invScreenR;
        const float d2 = nx * nx + ny * ny;

        int gray = 0;
        bool drawn = false;

        // OUTER SHELL: lifted shards (SomSphere lines 177-198 verbatim).
        if (d2 < outerR2)
        {
          const float zOuter = sqrtf(outerR2 - d2);
          const float hx = nx * invOuterR;
          const float hy = ny * invOuterR;
          const float hz = zOuter * invOuterR;

          // nearestSeed inlined.
          int cell = 0;
          float bestDot = -2.0f;
          for (int n = 0; n < 64; n++)
          {
            if (mNodeZ[n] < -0.1f) continue;
            const float d = hx * mNodeX[n] + hy * mNodeY[n] + hz * mNodeZ[n];
            if (d > bestDot) { bestDot = d; cell = n; }
          }
          const float cellR = 1.0f + mLiftVal[cell] * liftAmount;

          if (cellR > 0.95f * outerR && mLiftVal[cell] > 0.1f && d2 < cellR * cellR)
          {
            // TOP FACE of lifted shard.
            const float sb = mScanBright[cell];
            const float depthZ = hz;
            gray = 5 + (int)(sb * 8.0f);
            gray = (int)((float)gray * (0.3f + 0.7f * depthZ));
            if (gray > 13) gray = 13;
            if (gray < 3) gray = 3;
            drawn = true;
          }
        }

        // INNER SHELL: base surface (SomSphere lines 201-241 verbatim).
        if (!drawn && d2 < 1.0f)
        {
          const float zInner = sqrtf(1.0f - d2);
          const float hx = nx;
          const float hy = ny;
          const float hz = zInner;

          // twoNearestSeeds inlined.
          int cell = 0;
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
              second = cell;
              bestDot = d;
              cell = n;
            }
            else if (d > secondDot)
            {
              secondDot = d;
              second = n;
            }
          }
          (void)second;

          if (mLiftVal[cell] > 0.1f)
          {
            const float edgeRatio = secondDot / (bestDot + 0.001f);
            if (edgeRatio > 0.92f)
            {
              // SIDE WALL.
              const float sb = mScanBright[cell];
              gray = 3 + (int)(sb * 3.0f);
              gray = (int)((float)gray * (0.2f + 0.5f * hz));
              if (gray < 2) gray = 2;
            }
            // else: VOID -- gray stays 0.
          }
          else
          {
            // BASE SURFACE.
            const float edgeRatio = secondDot / (bestDot + 0.001f);
            const bool isEdge = edgeRatio > 0.98f;
            const float sb = mScanBright[cell];
            if (isEdge)
              gray = 4 + (int)(sb * 6.0f);
            else
              gray = 1 + (int)(sb * 7.0f);
            gray = (int)((float)gray * (0.3f + 0.7f * hz));
          }
          if (gray > 13) gray = 13;
        }

        mGrayCache[py * w + px] = (uint8_t)gray;
      }
    }
  }

  // ---- Per-frame draw -----------------------------------------------------
  __attribute__((optimize("no-tree-vectorize")))
  void AlembicSphereGraphic::draw(od::FrameBuffer &fb)
  {
    if (!mpAlembic) return;

    const int w = mWidth, h = mHeight;
    const int left = mWorldLeft, bot = mWorldBottom;
    const int scanNode = mpAlembic->getScanNode();

    // Camera target from precomputed per-node table.
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

    // Rotation matrix via LUT.
    const float cosRx = lutCos(mRotX);
    const float sinRx = lutSin(mRotX);
    const float cosRy = lutCos(mRotY);
    const float sinRy = lutSin(mRotY);

    // Fixed visual constants (SomSphere lines 104-109).
    const int minDim = w < h ? w : h;
    const float screenR = (float)minDim * 0.34f;
    const float liftAmount = 0.35f;
    const float cx = (float)w * 0.5f;
    const float cy = (float)h * 0.5f;
    const float nbrRadius = 0.5f;

    // Per-node liftVal + scanBright (SomSphere lines 116-130 verbatim,
    // with class-member arrays instead of stack-locals).
    const float scanSX = mNodeX0[scanNode];
    const float scanSY = mNodeY0[scanNode];
    const float scanSZ = mNodeZ0[scanNode];
    float maxLift = 0.0f;
    for (int n = 0; n < 64; n++)
    {
      // Phase 4 placeholder for richness: getNodeBrightness * 4. Phase 5
      // swaps for true training-derived richness.
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

    // Cache key check: rotation bucket or scan node changed -> invalidate.
    const float bucketScale = (float)kRotBuckets / (2.0f * (float)M_PI);
    const float bias = (float)kRotBuckets * 1000.0f;
    int newBucketX = (int)(mRotX * bucketScale + bias) % kRotBuckets;
    int newBucketY = (int)(mRotY * bucketScale + bias) % kRotBuckets;
    if (newBucketX != mRotBucketX || newBucketY != mRotBucketY ||
        scanNode != mScanNodeCached)
    {
      mRotBucketX = newBucketX;
      mRotBucketY = newBucketY;
      mScanNodeCached = scanNode;
      mRefreshProgress = 0;
    }

    // Time-sliced refresh: kRefreshFrames frames to re-render the full
    // grayscale cache after invalidation. 1/8 of rows per frame at h=64
    // is 8 rows / frame; w * 8 * ~80 ops = ~40K ops worst case during
    // refresh. Stable frames pay only the blit below.
    if (mRefreshProgress < h)
    {
      int rowsPerFrame = h / kRefreshFrames;
      if (rowsPerFrame < 1) rowsPerFrame = 1;
      int rowEnd = mRefreshProgress + rowsPerFrame;
      if (rowEnd > h) rowEnd = h;
      refreshCacheSlice(mRefreshProgress, rowEnd, outerR2, invOuterR);
      mRefreshProgress = rowEnd;
    }

    // Blit cache: paint every non-zero pixel from mGrayCache.
    for (int py = 0; py < h; py++)
    {
      const int rowBase = py * w;
      for (int px = 0; px < w; px++)
      {
        const uint8_t gray = mGrayCache[rowBase + px];
        if (gray > 0)
          fb.pixel((int)gray, left + px, bot + py);
      }
    }

    // Sphere outline ring at the equator (SomSphere lines 252-266 with
    // LUT cos/sin instead of runtime sinf/cosf).
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

    (void)nbrRadius; // kept as a named constant for parity with SomSphere
  }

} // namespace stolmine
