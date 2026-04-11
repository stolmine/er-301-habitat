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
    static const int kMaxClusters = 24;
    int mActiveClusters = 8;
    float mCx[kMaxClusters], mCy[kMaxClusters], mCz[kMaxClusters];
    float mCstr[kMaxClusters];

    float mNoisePhase[kMaxClusters];
    float mNoiseFreq[kMaxClusters];
    float mNoiseScale[kMaxClusters];

    // Work buffers (heap, not stack -- GUI thread has small stack on ARM)
    float mPts[256][3];
    float mSumX[kMaxClusters], mSumY[kMaxClusters], mSumZ[kMaxClusters];
    float mCount[kMaxClusters];
    float mScreenX[kMaxClusters], mScreenY[kMaxClusters], mScreenZ[kMaxClusters];
    float mProjStr[kMaxClusters];

    // Spectral complexity (zero-crossing rate) for brightness
    float mSpectralBright = 0.5f;
    // Spectral centroid proxy for shell radius
    float mCentroidSlew = 0.5f;

    // Frame skip: evaluate field every other frame, blit cache otherwise
    int mFrameToggle = 1; // start with eval frame, not blit
    uint8_t mCache[64 * 64]; // cached pixel values at full res

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth;
      int h = mHeight;
      int left = mWorldLeft;
      int bot = mWorldBottom;

      if (!mInitialized)
      {
        memset(mCache, 0, sizeof(mCache));
        // Seed clusters in a ring
        for (int i = 0; i < kMaxClusters; i++)
        {
          float angle = (float)i / (float)kMaxClusters * 6.28318f;
          mCx[i] = cosf(angle) * 0.3f;
          mCy[i] = sinf(angle) * 0.3f;
          mCz[i] = sinf(angle * 1.7f) * 0.2f;
          mCstr[i] = 0.5f;
          mNoisePhase[i] = angle;
          mNoiseFreq[i] = 0.03f + (float)i * 0.012f;
          mNoiseScale[i] = 1.0f;
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
      float (&pts)[256][3] = mPts;
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
      float (&sumX)[kMaxClusters] = mSumX;
      float (&sumY)[kMaxClusters] = mSumY;
      float (&sumZ)[kMaxClusters] = mSumZ;
      float (&count)[kMaxClusters] = mCount;
      for (int c = 0; c < mActiveClusters; c++)
      {
        sumX[c] = sumY[c] = sumZ[c] = 0.0f;
        count[c] = 0.0f;
      }

      for (int p = 0; p < nPts; p++)
      {
        // Find nearest cluster
        int best = 0;
        float bestD = 1e10f;
        for (int c = 0; c < mActiveClusters; c++)
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
      for (int c = 0; c < mActiveClusters; c++)
      {
        if (count[c] > 0.5f)
        {
          float nx = sumX[c] / count[c];
          float ny = sumY[c] / count[c];
          float nz = sumZ[c] / count[c];
          mCx[c] += (nx - mCx[c]) * 0.10f;
          mCy[c] += (ny - mCy[c]) * 0.10f;
          mCz[c] += (nz - mCz[c]) * 0.10f;
        }
        // Slew strength
        float targetStr = count[c] / (float)(nPts > 1 ? nPts : 1);
        mCstr[c] += (targetStr - mCstr[c]) * 0.08f;

        // Per-cluster noise LFO: advance phase, slew scale
        // ~30fps draw rate, so phase inc = freq / 30
        mNoisePhase[c] += mNoiseFreq[c] / 30.0f;
        if (mNoisePhase[c] > 1.0f) mNoisePhase[c] -= 1.0f;
        // Sine LFO mapped to 0.2-2.5 scale range
        float noiseTarget = 1.0f + sinf(mNoisePhase[c] * 6.28318f) * 0.8f;
        // At low frequencies: spikier distribution (wider range)
        // noiseFreq[c] is 0.3-1.3, lower = slower = more extreme
        float spikyness = 1.5f - mNoiseFreq[c];
        if (spikyness < 0.3f) spikyness = 0.3f;
        noiseTarget = 1.0f + (noiseTarget - 1.0f) * spikyness;
        mNoiseScale[c] += (noiseTarget - mNoiseScale[c]) * 0.04f;
      }

      // Project cluster centers to screen with 3D rotation
      float (&screenX)[kMaxClusters] = mScreenX;
      float (&screenY)[kMaxClusters] = mScreenY;
      float (&screenZ)[kMaxClusters] = mScreenZ;
      float (&projStr)[kMaxClusters] = mProjStr;
      for (int c = 0; c < mActiveClusters; c++)
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
        projStr[c] = mCstr[c] * mNoiseScale[c];
      }

      // Spectral complexity: zero-crossing rate on output ring buffer
      int crossings = 0;
      for (int i = 1; i < 256; i++)
      {
        float s0 = mpHelicase->getOutputSample(i - 1);
        float s1 = mpHelicase->getOutputSample(i);
        if ((s0 >= 0.0f && s1 < 0.0f) || (s0 < 0.0f && s1 >= 0.0f))
          crossings++;
      }
      // Normalize: pure sine ~2 crossings per cycle, rich FM = many more
      // Map to brightness: 0.3 (clean) to 1.0 (complex)
      float spectralTarget = 0.15f + (float)crossings / 60.0f * 0.85f;
      if (spectralTarget > 1.0f) spectralTarget = 1.0f;
      mSpectralBright += (spectralTarget - mSpectralBright) * 0.1f;

      // Active cluster count from spectral complexity: 6 (clean) to 24 (rich)
      mActiveClusters = 6 + (int)(mSpectralBright * 18.0f);
      if (mActiveClusters > kMaxClusters) mActiveClusters = kMaxClusters;
      if (mActiveClusters < 6) mActiveClusters = 6;

      // Spectral centroid proxy: mean absolute first difference / RMS
      // Higher ratio = brighter sound = tighter shell
      float sumDiff = 0.0f, sumSq = 0.0f;
      for (int i = 1; i < 256; i++)
      {
        float s0 = mpHelicase->getOutputSample(i - 1);
        float s1 = mpHelicase->getOutputSample(i);
        float d = s1 - s0;
        sumDiff += (d < 0 ? -d : d);
        sumSq += s1 * s1;
      }
      float rms = sqrtf(sumSq / 255.0f) + 0.001f;
      float centroidRaw = sumDiff / (255.0f * rms);
      // Map: low centroid (~0.5) = wide shell, high centroid (~3.0) = tight
      float centroidTarget = 1.0f - (centroidRaw - 0.5f) / 3.0f;
      if (centroidTarget < 0.3f) centroidTarget = 0.3f;
      if (centroidTarget > 0.95f) centroidTarget = 0.95f;
      mCentroidSlew += (centroidTarget - mCentroidSlew) * 0.08f;

      // Frame skip: expensive field eval every other frame, blit cache otherwise
      mFrameToggle ^= 1;
      if (mFrameToggle == 0)
      {
        // Fast blit from cache at full res
        for (int py = 0; py < h && py < 64; py++)
          for (int px = 0; px < w && px < 64; px++)
          {
            int gray = mCache[py * 64 + px];
            if (gray > 0)
              fb.pixel(gray, left + px, bot + py);
          }
        return;
      }

      // Full field evaluation frame
      float radX = (float)w * 0.5f;
      float radY = (float)h * 0.5f;
      float invRx = 1.0f / (radX > 1.0f ? radX : 1.0f);
      float invRy = 1.0f / (radY > 1.0f ? radY : 1.0f);
      float blobR2 = 0.04f;  // tight radius, distinct blobs
      float invBlobR2 = 1.0f / blobR2;

      // Render at full resolution, cache result
      memset(mCache, 0, sizeof(mCache));

      for (int py = 0; py < h; py++)
      {
        float ny = (float)py / (float)(h - 1);

        for (int px = 0; px < w; px++)
        {
          float nx = (float)px / (float)(w - 1);

          // Per-blob field contributions (track top two for voronoi)
          float field = 0.0f;
          float contrib[kMaxClusters];
          for (int c = 0; c < mActiveClusters; c++)
            contrib[c] = 0.0f;

          for (int c = 0; c < mActiveClusters; c++)
          {
            if (projStr[c] < 0.005f) continue;
            float dx = nx - screenX[c];
            float dy = ny - screenY[c];
            float d2 = dx * dx + dy * dy;
            if (d2 >= blobR2 * 1.6f) continue; // wider check for voronoi halo
            float t = 1.0f - d2 * invBlobR2;
            if (t < 0.0f) t = 0.0f;
            float depthRaw = 0.5f + 0.5f * (1.0f - screenZ[c]);
            float depthFade = 0.25f + 0.75f * depthRaw * depthRaw;
            float f = projStr[c] * 5.0f * t * t * t * depthFade;
            contrib[c] = f;
            field += f;
          }

          // Find top two contributors
          float top1 = 0.0f, top2 = 0.0f;
          for (int c = 0; c < mActiveClusters; c++)
          {
            if (contrib[c] > top1) { top2 = top1; top1 = contrib[c]; }
            else if (contrib[c] > top2) { top2 = contrib[c]; }
          }

          float sharp = field * field;
          int cellGray = 0;
          int blobGray = 0;
          bool isBlob = sharp > 0.04f;

          if (isBlob)
          {
            float blob = (sharp - 0.04f) * 30.0f;
            if (blob > 13.0f) blob = 13.0f;
            blobGray = (int)(blob * mSpectralBright);
            if (blobGray > 13) blobGray = 13;
            cellGray = blobGray;
          }

          // Voronoi edge: thin seam floating just outside blob surface
          // Only draw where two blobs have nearly equal influence (tight ratio)
          if (top1 > 0.01f && top2 > 0.01f)
          {
            float ratio = top2 / top1; // 0=dominated, 1=equal
            // Very narrow edge band: ratio must be close to 1.0
            // and we must be near (slightly outside) the surface
            bool nearSurface = (field > 0.03f && sharp <= 0.10f) ||
                               (sharp > 0.04f && ratio > 0.80f);
            if (ratio > 0.70f && nearSurface)
            {
              float edgeBright = (ratio - 0.70f) / 0.30f;
              edgeBright = edgeBright * edgeBright;

              // Depth from the two contributing blobs
              float edgeDepth = 0.5f;
              int best1 = 0, best2 = 0;
              float b1 = 0, b2 = 0;
              for (int c = 0; c < mActiveClusters; c++)
              {
                if (contrib[c] > b1) { b2 = b1; best2 = best1; b1 = contrib[c]; best1 = c; }
                else if (contrib[c] > b2) { b2 = contrib[c]; best2 = c; }
              }
              edgeDepth = 0.7f + 0.3f * (0.5f * (
                (0.5f + 0.5f * (1.0f - screenZ[best1])) +
                (0.5f + 0.5f * (1.0f - screenZ[best2]))));
              if (edgeDepth > 1.0f) edgeDepth = 1.0f;

              // Bead stipple: brightness pulses along edge for texture
              float bead = 0.6f + 0.4f * sinf((nx + ny) * 120.0f);

              // Edge Z: average of two parent blob Z positions
              float edgeZ = (screenZ[best1] + screenZ[best2]) * 0.5f;

              if (isBlob)
              {
                // Find the dominant blob's Z at this pixel
                float blobZ = screenZ[best1]; // strongest contributor
                // Only carve dark line if edge is in front of blob surface
                if (edgeZ <= blobZ + 0.1f)
                {
                  // Dark line: scales down toward black as blob gets brighter
                  int darkTarget = 0;
                  cellGray = darkTarget + (int)((float)(blobGray - darkTarget) * (1.0f - edgeBright * bead));
                  if (cellGray < darkTarget) cellGray = darkTarget;
                  if (cellGray > blobGray) cellGray = blobGray;
                }
                // else: blob occludes edge, keep blob gray
              }
              else
              {
                // Void: bright structural edge, high floor
                cellGray = 3 + (int)(edgeBright * 7.0f * mSpectralBright * edgeDepth * bead);
                if (cellGray > 9) cellGray = 9;
              }
            }
          }

          // Write to cache and screen
          if (px < 64 && py < 64)
            mCache[py * 64 + px] = (uint8_t)cellGray;
          if (cellGray > 0)
            fb.pixel(cellGray, left + px, bot + py);
        }
      }

    }
  };

} // namespace stolmine
