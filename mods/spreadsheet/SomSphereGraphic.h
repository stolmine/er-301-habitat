#pragma once

#include <od/graphics/Graphic.h>
#include "Som.h"
#include <math.h>
#include <string.h>

namespace stolmine
{

  class SomSphereGraphic : public od::Graphic
  {
  public:
    SomSphereGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpSom(0) {}

    virtual ~SomSphereGraphic()
    {
      if (mpSom)
        mpSom->release();
    }

    void follow(Som *p)
    {
      if (mpSom)
        mpSom->release();
      mpSom = p;
      if (mpSom)
        mpSom->attach();
    }

  private:
    Som *mpSom;
    float mRotX = 0.0f, mRotY = 0.0f, mRotZ = 0.0f;
    float mTargetRotX = 0.0f, mTargetRotY = 0.0f;
    int mFrameToggle = 1;
    uint8_t mCache[64 * 64] = {};
    // Cached projected positions + cell data
    float mProjX[64], mProjY[64];
    int mCellGray[64];
    int mLastScanNode = -1;
    float mRotDelta = 0.0f; // integrator for rotation momentum

#ifndef SWIGLUA
  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth, h = mHeight;
      int left = mWorldLeft, bot = mWorldBottom;

      if (!mpSom)
        return;

      int scanNode = mpSom->getScanNode();

      // Scan-following rotation with integrator slew
      {
        float sx = mpSom->getNodeX(scanNode);
        float sy = mpSom->getNodeY(scanNode);
        float sz = mpSom->getNodeZ(scanNode);
        mTargetRotX = asinf(sy < -1.0f ? -1.0f : (sy > 1.0f ? 1.0f : sy));
        mTargetRotY = atan2f(sx, sz);

        float dx = mTargetRotX - mRotX;
        float dy = mTargetRotY - mRotY;
        // Wrap Y delta to shortest path
        if (dy > 3.14159f) dy -= 6.28318f;
        if (dy < -3.14159f) dy += 6.28318f;
        float dist = sqrtf(dx * dx + dy * dy);

        // Integrator: small jumps = slow walks, large jumps = snappier
        float slewRate = 0.02f + dist * 0.12f;
        mRotX += dx * slewRate;
        mRotY += dy * slewRate;
      }
      // Z-axis: static (no constant drift)

      float cosRx = cosf(mRotX), sinRx = sinf(mRotX);
      float cosRy = cosf(mRotY), sinRy = sinf(mRotY);
      float cosRz = cosf(mRotZ), sinRz = sinf(mRotZ);

      int minDim = w < h ? w : h;
      float baseRadius = (float)minDim * 0.40f;
      float liftAmount = (float)minDim * 0.12f;
      float cx = (float)w * 0.5f;
      float cy = (float)h * 0.5f;

      // Compute lift per node (proximity to scan position in chain)
      float lift[64];
      float nbrRadius = 0.3f; // visual neighborhood for lifting
      for (int n = 0; n < 64; n++)
      {
        // Use sphere distance to scan node for lift falloff
        float ndx = mpSom->getNodeX(n) - mpSom->getNodeX(scanNode);
        float ndy = mpSom->getNodeY(n) - mpSom->getNodeY(scanNode);
        float ndz = mpSom->getNodeZ(n) - mpSom->getNodeZ(scanNode);
        float sphereDist = sqrtf(ndx * ndx + ndy * ndy + ndz * ndz);
        float lf = 1.0f - sphereDist / nbrRadius;
        lift[n] = (lf > 0.0f) ? lf * lf : 0.0f; // quadratic falloff
      }

      // Project all 64 nodes
      for (int n = 0; n < 64; n++)
      {
        float x = mpSom->getNodeX(n);
        float y = mpSom->getNodeY(n);
        float z = mpSom->getNodeZ(n);

        // Lift: push outward from sphere center
        float r = 1.0f + lift[n] * 0.4f;
        x *= r; y *= r; z *= r;

        // Y-axis rotation
        float rx = x * cosRy - z * sinRy;
        float rz = x * sinRy + z * cosRy;
        // X-axis tilt
        float ty = y * cosRx - rz * sinRx;
        float tz = y * sinRx + rz * cosRx;
        // Z-axis roll
        float fx = rx * cosRz - ty * sinRz;
        float fy = rx * sinRz + ty * cosRz;

        mProjX[n] = cx + fx * baseRadius;
        mProjY[n] = cy + fy * baseRadius;

        // Cell brightness: training richness (weight vector variance)
        float variance = 0.0f;
        for (int d = 0; d < 6; d++)
        {
          float w = mpSom->getNodeWeight(n, d);
          variance += w * w;
        }
        variance = sqrtf(variance / 6.0f);

        // Depth shading
        float depth = (tz + 1.5f) / 3.0f;
        if (depth < 0.0f) depth = 0.0f;
        if (depth > 1.0f) depth = 1.0f;

        // Brightness from training richness + depth + lift bonus
        float bright = depth * (0.2f + variance * 0.8f) + lift[n] * 0.3f;
        int gray = 1 + (int)(bright * 12.0f);
        if (gray < 1) gray = 1;
        if (gray > 13) gray = 13;

        mCellGray[n] = gray;
      }

      // Frame caching on the Voronoi assignment
      mFrameToggle ^= 1;
      if (mFrameToggle == 0)
      {
        for (int py = 0; py < h && py < 64; py++)
          for (int px = 0; px < w && px < 64; px++)
          {
            int gray = mCache[py * 64 + px];
            if (gray > 0)
              fb.pixel(gray, left + px, bot + py);
          }
        return;
      }

      // Per-pixel Voronoi: find nearest projected node
      memset(mCache, 0, sizeof(mCache));

      for (int py = 0; py < h && py < 64; py++)
      {
        float screenY = (float)py;
        for (int px = 0; px < w && px < 64; px++)
        {
          float screenX = (float)px;

          // Check if pixel is within sphere silhouette (rough circle test)
          float dx = screenX - cx;
          float dy = screenY - cy;
          float d2 = dx * dx + dy * dy;
          float maxR = baseRadius + liftAmount;
          if (d2 > maxR * maxR) continue;

          // Find nearest node
          float bestDist = 1e10f;
          int bestNode = 0;
          for (int n = 0; n < 64; n++)
          {
            float ndx = screenX - mProjX[n];
            float ndy = screenY - mProjY[n];
            float nd2 = ndx * ndx + ndy * ndy;
            if (nd2 < bestDist) { bestDist = nd2; bestNode = n; }
          }

          int gray = mCellGray[bestNode];

          // Find second-closest node for Voronoi edge detection
          float secondBest = 1e10f;
          int secondNode = 0;
          for (int n = 0; n < 64; n++)
          {
            if (n == bestNode) continue;
            float ndx = screenX - mProjX[n];
            float ndy = screenY - mProjY[n];
            float nd2 = ndx * ndx + ndy * ndy;
            if (nd2 < secondBest) { secondBest = nd2; secondNode = n; }
          }

          // Voronoi edge: ratio of distances to two nearest nodes
          float d1 = sqrtf(bestDist);
          float d2s = sqrtf(secondBest);
          float ratio = d1 / (d2s + 0.01f);

          // Tight edge band with bead stipple (Helicase pattern)
          if (ratio > 0.80f)
          {
            float edgeBright = (ratio - 0.80f) / 0.20f;
            edgeBright = edgeBright * edgeBright;
            // Bead stipple texture along edge
            float bead = 0.6f + 0.4f * sinf((screenX + screenY) * 80.0f);
            // Contour: bright edge line on darker cell fill
            int edgeGray = 2 + (int)(edgeBright * bead * 8.0f);
            if (edgeGray > 10) edgeGray = 10;
            gray = (gray > edgeGray) ? gray - edgeGray / 2 : 1;
          }

          // Sphere edge fade
          float edgeDist = sqrtf(d2) / baseRadius;
          float maxEdge = 1.0f + liftAmount / baseRadius;
          if (edgeDist > 0.85f && edgeDist <= maxEdge)
          {
            float fade = 1.0f - (edgeDist - 0.85f) / (maxEdge - 0.85f);
            if (fade < 0.0f) fade = 0.0f;
            gray = (int)((float)gray * fade);
          }

          if (gray > 0)
          {
            mCache[py * 64 + px] = (uint8_t)gray;
            fb.pixel(gray, left + px, bot + py);
          }
        }
      }
    }
#endif
  };

} // namespace stolmine
