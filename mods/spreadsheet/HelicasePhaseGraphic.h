#pragma once

#include <od/graphics/Graphic.h>
#include "Helicase.h"
#include <math.h>
#include <string.h>

namespace stolmine
{

  class HelicasePhaseGraphic : public od::Graphic
  {
  public:
    HelicasePhaseGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpHelicase(0) {}

    virtual ~HelicasePhaseGraphic()
    {
      if (mpHelicase)
        mpHelicase->release();
    }

    void follow(Helicase *p)
    {
      if (mpHelicase)
        mpHelicase->release();
      mpHelicase = p;
      if (mpHelicase)
        mpHelicase->attach();
    }

  private:
    Helicase *mpHelicase;
    float mRotAngle = 0.0f;
    float mMinX = -1.0f, mMaxX = 1.0f;
    float mMinY = -1.0f, mMaxY = 1.0f;
    bool mInitialized = false;

    // Cluster centroids in 3D (slewed)
    static const int kClusters = 6;
    float mCx[kClusters], mCy[kClusters], mCz[kClusters];
    float mCstr[kClusters]; // cluster strength (number of assigned points)

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth;
      int h = mHeight;
      int left = mWorldLeft;
      int bot = mWorldBottom;

      if (!mInitialized)
      {
        // Seed clusters in a ring
        for (int i = 0; i < kClusters; i++)
        {
          float angle = (float)i / (float)kClusters * 6.28318f;
          mCx[i] = cosf(angle) * 0.3f;
          mCy[i] = sinf(angle) * 0.3f;
          mCz[i] = 0.0f;
          mCstr[i] = 0.5f;
        }
        mInitialized = true;
      }

      fb.fill(BLACK, left, bot, left + w - 1, bot + h - 1);

      if (!mpHelicase)
        return;

      // Rotation
      mRotAngle += 0.012f;
      if (mRotAngle > 6.28318f)
        mRotAngle -= 6.28318f;
      float cosR = cosf(mRotAngle);
      float sinR = sinf(mRotAngle);
      float tiltCos = 0.85f;
      float tiltSin = 0.53f;

      // Auto-scale
      float expandRate = 0.4f;
      float contractRate = 0.01f;

      // Collect normalized 3D points from ring buffer
      float pts[256][3];
      int nPts = 0;
      for (int i = 2; i < 256; i++)
      {
        float x = mpHelicase->getOutputSample(i);
        float y = mpHelicase->getOutputSample(i - 1);
        float z = mpHelicase->getOutputSample(i - 2);

        if (x < mMinX) mMinX += (x - mMinX) * expandRate;
        if (x > mMaxX) mMaxX += (x - mMaxX) * expandRate;
        if (y < mMinY) mMinY += (y - mMinY) * expandRate;
        if (y > mMaxY) mMaxY += (y - mMaxY) * expandRate;
        mMinX += (-1.0f - mMinX) * contractRate;
        mMaxX += (1.0f - mMaxX) * contractRate;
        mMinY += (-1.0f - mMinY) * contractRate;
        mMaxY += (1.0f - mMaxY) * contractRate;

        float rangeX = mMaxX - mMinX;
        float rangeY = mMaxY - mMinY;
        if (rangeX < 0.01f) rangeX = 0.01f;
        if (rangeY < 0.01f) rangeY = 0.01f;

        pts[nPts][0] = (x - (mMinX + mMaxX) * 0.5f) / (rangeX * 0.5f);
        pts[nPts][1] = (y - (mMinY + mMaxY) * 0.5f) / (rangeY * 0.5f);
        pts[nPts][2] = (z - (mMinX + mMaxX) * 0.5f) / (rangeX * 0.5f);
        nPts++;
      }

      // One iteration of k-means: assign points, compute new centroids
      float sumX[kClusters], sumY[kClusters], sumZ[kClusters];
      float count[kClusters];
      for (int c = 0; c < kClusters; c++)
      {
        sumX[c] = sumY[c] = sumZ[c] = 0.0f;
        count[c] = 0.0f;
      }

      for (int p = 0; p < nPts; p++)
      {
        // Find nearest cluster
        int best = 0;
        float bestD = 1e10f;
        for (int c = 0; c < kClusters; c++)
        {
          float dx = pts[p][0] - mCx[c];
          float dy = pts[p][1] - mCy[c];
          float dz = pts[p][2] - mCz[c];
          float d = dx * dx + dy * dy + dz * dz;
          if (d < bestD) { bestD = d; best = c; }
        }
        sumX[best] += pts[p][0];
        sumY[best] += pts[p][1];
        sumZ[best] += pts[p][2];
        count[best] += 1.0f;
      }

      // Slew centroids toward new means
      for (int c = 0; c < kClusters; c++)
      {
        if (count[c] > 0.5f)
        {
          float nx = sumX[c] / count[c];
          float ny = sumY[c] / count[c];
          float nz = sumZ[c] / count[c];
          mCx[c] += (nx - mCx[c]) * 0.15f;
          mCy[c] += (ny - mCy[c]) * 0.15f;
          mCz[c] += (nz - mCz[c]) * 0.15f;
        }
        // Slew strength
        float targetStr = count[c] / (float)(nPts > 1 ? nPts : 1);
        mCstr[c] += (targetStr - mCstr[c]) * 0.12f;
      }

      // Project cluster centers to screen with 3D rotation
      float screenX[kClusters], screenY[kClusters], screenZ[kClusters];
      float projStr[kClusters];
      for (int c = 0; c < kClusters; c++)
      {
        // Rotate around Y
        float rx = mCx[c] * cosR - mCz[c] * sinR;
        float rz = mCx[c] * sinR + mCz[c] * cosR;
        // Tilt around X
        float ty = mCy[c] * tiltCos - rz * tiltSin;
        float tz = mCy[c] * tiltSin + rz * tiltCos;

        screenX[c] = rx * 0.58f + 0.5f;
        screenY[c] = ty * 0.58f + 0.5f;
        screenZ[c] = tz;
        projStr[c] = mCstr[c];
      }

      // Render Sfera-style metaball field
      float radX = (float)w * 0.5f;
      float radY = (float)h * 0.5f;
      float invRx = 1.0f / (radX > 1.0f ? radX : 1.0f);
      float invRy = 1.0f / (radY > 1.0f ? radY : 1.0f);
      float blobR2 = 0.04f;  // tight radius, distinct blobs
      float invBlobR2 = 1.0f / blobR2;

      for (int py = 0; py < h; py++)
      {
        float ny = (float)py / (float)(h - 1);

        for (int px = 0; px < w; px++)
        {
          float nx = (float)px / (float)(w - 1);

          float field = 0.0f;
          for (int c = 0; c < kClusters; c++)
          {
            if (projStr[c] < 0.005f) continue;
            float dx = nx - screenX[c];
            float dy = ny - screenY[c];
            float d2 = dx * dx + dy * dy;
            if (d2 >= blobR2) continue;
            float t = 1.0f - d2 * invBlobR2;
            // Depth lighting: distant light source, range compressed
            // Far blobs dim but visible, near blobs bright but not blown out
            float depthRaw = 0.5f + 0.5f * (1.0f - screenZ[c]); // 0=far, 1=near
            float depthFade = 0.25f + 0.75f * depthRaw * depthRaw; // quadratic, floor 0.25
            field += projStr[c] * 5.0f * t * t * t * depthFade;
          }

          if (field > 0.15f)
          {
            float blob = (field - 0.15f) * 20.0f;
            if (blob > 13.0f) blob = 13.0f;
            int gray = (int)blob;
            if (gray > 0)
              fb.pixel(gray, left + px, bot + py);
          }
        }
      }
    }
  };

} // namespace stolmine
