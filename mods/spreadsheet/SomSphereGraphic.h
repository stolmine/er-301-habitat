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

    // Precomputed sphere distances from scan node (updated per frame)
    float mScanDist[64];

#ifndef SWIGLUA
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
        float slewRate = 0.02f + dist * 0.12f;
        mRotX += dx * slewRate;
        mRotY += dy * slewRate;
      }

      float cosRx = cosf(mRotX), sinRx = sinf(mRotX);
      float cosRy = cosf(mRotY), sinRy = sinf(mRotY);

      int minDim = w < h ? w : h;
      float baseRadius = (float)minDim * 0.37f;
      float liftAmount = 0.4f;
      float cx = (float)w * 0.5f;
      float cy = (float)h * 0.5f;
      float nbrRadius = 0.5f; // visual neighborhood for brightness falloff

      // Compute sphere distances from scan node + scan proximity brightness per node
      float scanSphereX = mpSom->getNodeX(scanNode);
      float scanSphereY = mpSom->getNodeY(scanNode);
      float scanSphereZ = mpSom->getNodeZ(scanNode);

      float lift[64], proxBright[64], richness[64];
      for (int n = 0; n < 64; n++)
      {
        float dx = mpSom->getNodeX(n) - scanSphereX;
        float dy = mpSom->getNodeY(n) - scanSphereY;
        float dz = mpSom->getNodeZ(n) - scanSphereZ;
        float sd = sqrtf(dx * dx + dy * dy + dz * dz);
        mScanDist[n] = sd;

        float lf = 1.0f - sd / nbrRadius;
        lift[n] = (lf > 0.0f) ? lf * lf : 0.0f;

        float pb = 1.0f - sd / (nbrRadius * 1.5f);
        proxBright[n] = (pb > 0.0f) ? pb * pb : 0.0f;

        float r = mpSom->getNodeRichness(n);
        richness[n] = r / (r + 2.0f); // sigmoid normalize
      }

      // Project all 64 nodes
      float projX[64], projY[64], projZ[64];
      for (int n = 0; n < 64; n++)
      {
        float x = mpSom->getNodeX(n);
        float y = mpSom->getNodeY(n);
        float z = mpSom->getNodeZ(n);

        float rx = x * cosRy - z * sinRy;
        float rz = x * sinRy + z * cosRy;
        float ty = y * cosRx - rz * sinRx;
        float tz = y * sinRx + rz * cosRx;

        // Screen-space outward push: lifted cells move away from sphere center on screen
        float screenRx = rx * baseRadius;
        float screenTy = ty * baseRadius;
        float screenDist = sqrtf(screenRx * screenRx + screenTy * screenTy);
        if (screenDist > 0.1f && lift[n] > 0.0f)
        {
          float pushAmount = lift[n] * liftAmount * baseRadius;
          screenRx += (screenRx / screenDist) * pushAmount;
          screenTy += (screenTy / screenDist) * pushAmount;
        }

        projX[n] = cx + screenRx;
        projY[n] = cy + screenTy;
        projZ[n] = tz;
      }

      // Per-pixel Voronoi rendering
      float sphereR = baseRadius * (1.0f + liftAmount * 0.5f);
      float sphereR2 = sphereR * sphereR;

      for (int py = 0; py < h; py++)
      {
        float screenY = (float)py;
        for (int px = 0; px < w; px++)
        {
          float screenX = (float)px;

          // Sphere silhouette
          float sdx = screenX - cx;
          float sdy = screenY - cy;
          if (sdx * sdx + sdy * sdy > sphereR2) continue;

          // Find two nearest front-facing nodes (projZ >= -0.1)
          float bestDist = 1e10f, secondDist = 1e10f;
          int bestNode = -1, secondNode = -1;
          for (int n = 0; n < 64; n++)
          {
            if (projZ[n] < -0.1f) continue; // back-face cull
            float ndx = screenX - projX[n];
            float ndy = screenY - projY[n];
            float nd2 = ndx * ndx + ndy * ndy;
            if (nd2 < bestDist)
            {
              secondDist = bestDist; secondNode = bestNode;
              bestDist = nd2; bestNode = n;
            }
            else if (nd2 < secondDist)
            {
              secondDist = nd2; secondNode = n;
            }
          }
          if (bestNode < 0) continue; // no front-facing nodes near this pixel

          // Depth shading from best node's Z
          float depth = (projZ[bestNode] + 1.5f) / 3.0f;
          depth = CLAMP(0.15f, 1.0f, depth);

          // Edge detection: distance ratio
          float d1 = sqrtf(bestDist);
          float d2 = sqrtf(secondDist);
          float ratio = d1 / (d2 + 0.01f);
          bool isEdge = ratio > 0.85f;

          float rich = richness[bestNode];

          int gray = 0;

          if (isEdge)
          {
            // Voronoi contour: always visible
            float edgeBright = (ratio - 0.85f) / 0.15f;
            if (edgeBright > 1.0f) edgeBright = 1.0f;
            gray = 5 + (int)(edgeBright * 7.0f);
          }
          else
          {
            // Solid fill: training richness only
            gray = (int)(rich * 10.0f);
          }

          // Apply depth shading
          gray = (int)((float)gray * depth);

          if (gray > 13) gray = 13;
          if (gray > 0)
            fb.pixel(gray, left + px, bot + py);
        }
      }

      // Sphere outline circle
      int steps = 64;
      for (int i = 0; i < steps; i++)
      {
        float a = (float)i / (float)steps * 6.28318f;
        int ox = left + (int)(cx + cosf(a) * sphereR);
        int oy = bot + (int)(cy + sinf(a) * sphereR);
        if (ox >= left && ox < left + w && oy >= bot && oy < bot + h)
          fb.pixel(GRAY5, ox, oy);
      }
    }
#endif
  };

} // namespace stolmine
