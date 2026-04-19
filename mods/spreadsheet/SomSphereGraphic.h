#pragma once

#include <od/graphics/Graphic.h>
#include "Som.h"
#include <math.h>

namespace stolmine
{

  class SomSphereGraphic : public od::Graphic
  {
  public:
    SomSphereGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpSom(0) {}

    virtual ~SomSphereGraphic()
    {
      if (mpSom) mpSom->release();
    }

    void follow(Som *p)
    {
      if (mpSom) mpSom->release();
      mpSom = p;
      if (mpSom) mpSom->attach();
    }

  private:
    Som *mpSom;
    float mRotX = 0.0f, mRotY = 0.0f;
    float mTargetRotX = 0.0f, mTargetRotY = 0.0f;
    int mFrameToggle = 1;
    uint8_t mCache[64 * 64] = {};

#ifndef SWIGLUA

    // Find nearest seed by dot product (spherical Voronoi)
    int nearestSeed(float *sx, float *sy, float *sz,
                    float hx, float hy, float hz, int count)
    {
      int best = 0;
      float bestDot = -2.0f;
      for (int n = 0; n < count; n++)
      {
        if (sz[n] < -0.1f) continue; // back-face cull
        float d = hx * sx[n] + hy * sy[n] + hz * sz[n];
        if (d > bestDot) { bestDot = d; best = n; }
      }
      return best;
    }

    // Find two nearest seeds + dot products
    void twoNearestSeeds(float *sx, float *sy, float *sz,
                         float hx, float hy, float hz, int count,
                         int &best, int &second, float &bestDot, float &secondDot)
    {
      best = 0; second = 0;
      bestDot = -2.0f; secondDot = -2.0f;
      for (int n = 0; n < count; n++)
      {
        if (sz[n] < -0.1f) continue;
        float d = hx * sx[n] + hy * sy[n] + hz * sz[n];
        if (d > bestDot)
        {
          secondDot = bestDot; second = best;
          bestDot = d; best = n;
        }
        else if (d > secondDot)
        {
          secondDot = d; second = n;
        }
      }
    }

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth, h = mHeight;
      int left = mWorldLeft, bot = mWorldBottom;

      if (!mpSom) return;

      int scanNode = mpSom->getScanNode();

      // Scan-following rotation with integrator slew
      {
        float sx = mpSom->getNodeX(scanNode);
        float sy = mpSom->getNodeY(scanNode);
        float sz = mpSom->getNodeZ(scanNode);
        mTargetRotX = asinf(CLAMP(-1.0f, 1.0f, sy)) - 0.3f;
        mTargetRotY = atan2f(sx, sz) + 0.25f;
        float dx = mTargetRotX - mRotX;
        float dy = mTargetRotY - mRotY;
        if (dy > 3.14159f) dy -= 6.28318f;
        if (dy < -3.14159f) dy += 6.28318f;
        float dist = sqrtf(dx * dx + dy * dy);
        mRotX += dx * (0.02f + dist * 0.12f);
        mRotY += dy * (0.02f + dist * 0.12f);
      }

      float cosRx = cosf(mRotX), sinRx = sinf(mRotX);
      float cosRy = cosf(mRotY), sinRy = sinf(mRotY);

      int minDim = w < h ? w : h;
      float screenR = (float)minDim * 0.34f;
      float liftAmount = 0.35f;
      float cx = (float)w * 0.5f;
      float cy = (float)h * 0.5f;
      float nbrRadius = 0.5f;

      // Per-node data
      float scanSX = mpSom->getNodeX(scanNode);
      float scanSY = mpSom->getNodeY(scanNode);
      float scanSZ = mpSom->getNodeZ(scanNode);

      float liftVal[64], scanBright[64];
      float maxLift = 0.0f;
      for (int n = 0; n < 64; n++)
      {
        float r = mpSom->getNodeRichness(n);
        liftVal[n] = r / (r + 2.0f);
        if (liftVal[n] > maxLift) maxLift = liftVal[n];

        float dx = mpSom->getNodeX(n) - scanSX;
        float dy = mpSom->getNodeY(n) - scanSY;
        float dz = mpSom->getNodeZ(n) - scanSZ;
        float sd = sqrtf(dx * dx + dy * dy + dz * dz);
        float pb = 1.0f - sd / nbrRadius;
        scanBright[n] = (pb > 0.0f) ? pb * pb : 0.0f;
      }

      // Rotate all 64 seeds once per frame
      float rotX[64], rotY[64], rotZ[64];
      for (int n = 0; n < 64; n++)
      {
        float x = mpSom->getNodeX(n);
        float y = mpSom->getNodeY(n);
        float z = mpSom->getNodeZ(n);
        float rx = x * cosRy - z * sinRy;
        float rz = x * sinRy + z * cosRy;
        rotX[n] = rx;
        rotY[n] = y * cosRx - rz * sinRx;
        rotZ[n] = y * sinRx + rz * cosRx;
      }

      // Frame caching
      mFrameToggle ^= 1;
      if (mFrameToggle == 0)
      {
        for (int py = 0; py < h && py < 64; py++)
          for (int px = 0; px < w && px < 64; px++)
          {
            int gray = mCache[py * 64 + px];
            if (gray > 0) fb.pixel(gray, left + px, bot + py);
          }
        return;
      }

      memset(mCache, 0, sizeof(mCache));

      float outerR = 1.0f + maxLift * liftAmount;
      float outerR2 = outerR * outerR;
      float invScreenR = 1.0f / screenR;

      for (int py = 0; py < h && py < 64; py++)
      {
        for (int px = 0; px < w && px < 64; px++)
        {
          float nx = ((float)px - cx) * invScreenR;
          float ny = ((float)py - cy) * invScreenR;
          float d2 = nx * nx + ny * ny;

          int gray = 0;
          bool drawn = false;

          // Test OUTER shell: lifted shards
          if (d2 < outerR2)
          {
            float zOuter = sqrtf(outerR2 - d2);
            float invR = 1.0f / outerR;
            float hx = nx * invR, hy = ny * invR, hz = zOuter * invR;

            int cell = nearestSeed(rotX, rotY, rotZ, hx, hy, hz, 64);
            float cellR = 1.0f + liftVal[cell] * liftAmount;

            // Does this cell actually extend to this shell radius?
            if (cellR > 0.95f * outerR && liftVal[cell] > 0.1f && d2 < cellR * cellR)
            {
              // TOP FACE of lifted shard
              float sb = scanBright[cell];
              float depthZ = hz;
              gray = 5 + (int)(sb * 8.0f);
              gray = (int)((float)gray * (0.3f + 0.7f * depthZ));
              if (gray > 13) gray = 13;
              if (gray < 3) gray = 3;
              drawn = true;
            }
          }

          // Test INNER shell: base surface
          if (!drawn && d2 < 1.0f)
          {
            float zInner = sqrtf(1.0f - d2);
            float hx = nx, hy = ny, hz = zInner;

            int cell, second;
            float bestDot, secondDot;
            twoNearestSeeds(rotX, rotY, rotZ, hx, hy, hz, 64,
                            cell, second, bestDot, secondDot);

            if (liftVal[cell] > 0.1f)
            {
              // VOID: this cell has lifted away
              float edgeRatio = secondDot / (bestDot + 0.001f);
              if (edgeRatio > 0.92f)
              {
                // SIDE WALL: near boundary of lifted cell
                float sb = scanBright[cell];
                gray = 3 + (int)(sb * 3.0f);
                gray = (int)((float)gray * (0.2f + 0.5f * hz));
                if (gray < 2) gray = 2;
              }
              // else: void, leave black
            }
            else
            {
              // BASE SURFACE: un-lifted cell
              float edgeRatio = secondDot / (bestDot + 0.001f);
              bool isEdge = edgeRatio > 0.95f;
              float sb = scanBright[cell];

              if (isEdge)
                gray = 4 + (int)(sb * 6.0f);
              else
                gray = 1 + (int)(sb * 7.0f);

              gray = (int)((float)gray * (0.3f + 0.7f * hz));
            }

            if (gray > 13) gray = 13;
          }

          if (gray > 0)
          {
            mCache[py * 64 + px] = (uint8_t)gray;
            fb.pixel(gray, left + px, bot + py);
          }
        }
      }

      // Inner sphere outline
      {
        int steps = 64;
        int prevOx = -1, prevOy = -1;
        for (int i = 0; i <= steps; i++)
        {
          float a = (float)(i % steps) / (float)steps * 6.28318f;
          int ox = left + (int)(cx + cosf(a) * screenR);
          int oy = bot + (int)(cy + sinf(a) * screenR);
          if (prevOx >= 0 &&
              ox >= left && ox < left + w && oy >= bot && oy < bot + h &&
              prevOx >= left && prevOx < left + w && prevOy >= bot && prevOy < bot + h)
            fb.line(GRAY3, prevOx, prevOy, ox, oy);
          prevOx = ox; prevOy = oy;
        }
      }
    }
#endif
  };

} // namespace stolmine
