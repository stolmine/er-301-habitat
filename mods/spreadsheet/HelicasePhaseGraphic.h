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

    // Per-cluster noise LFO for size variation
    float mNoisePhase[kClusters];
    float mNoiseFreq[kClusters];
    float mNoiseScale[kClusters]; // slewed noise output per cluster

    // Spectral complexity (zero-crossing rate) for brightness
    float mSpectralBright = 0.5f;
    // Spectral centroid proxy for shell radius
    float mCentroidSlew = 0.5f;

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
          // Each cluster gets a different LFO rate (0.3-1.5 Hz range)
          mNoisePhase[i] = angle; // stagger phases
          mNoiseFreq[i] = 0.06f + (float)i * 0.04f;
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
        mNoiseScale[c] += (noiseTarget - mNoiseScale[c]) * 0.06f;
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

          // Steepen transition: square the field before thresholding
          float sharp = field * field;
          if (sharp > 0.04f)
          {
            float blob = (sharp - 0.04f) * 30.0f;
            if (blob > 13.0f) blob = 13.0f;
            int gray = (int)(blob * mSpectralBright);
            if (gray > 13) gray = 13;
            if (gray > 0)
              fb.pixel(gray, left + px, bot + py);
          }
        }
      }

      // 3D voronoi shell: sphere with radius from spectral centroid
      // Sample points on the sphere surface, project with same rotation as blobs
      // Voronoi edges where nearest blob center (in 3D) switches
      float shellR = mCentroidSlew * 0.7f; // in normalized coordinates
      static const int kLatSteps = 16;
      static const int kLonSteps = 32;

      for (int lat = 1; lat < kLatSteps; lat++)
      {
        float phi = (float)lat / (float)kLatSteps * 3.14159f; // 0 to pi
        float sinPhi = sinf(phi);
        float cosPhi = cosf(phi);

        for (int lon = 0; lon < kLonSteps; lon++)
        {
          float theta = (float)lon / (float)kLonSteps * 6.28318f;

          // Point on unit sphere
          float spx = sinPhi * cosf(theta) * shellR;
          float spy = cosPhi * shellR;
          float spz = sinPhi * sinf(theta) * shellR;

          // Rotate with same transform as blobs
          float rx = spx * cosR - spz * sinR;
          float rz = spx * sinR + spz * cosR;
          float ty = spy * tiltCos - rz * tiltSin;
          float tz = spy * tiltSin + rz * tiltCos;

          // Back-face cull: skip points facing away
          if (tz < -shellR * 0.3f) continue;

          // Screen position
          float snx = rx * 0.58f + 0.5f;
          float sny = ty * 0.58f + 0.5f;

          // Find nearest and second-nearest blob (in 3D, pre-projection)
          float nearest = 1e10f, secondNearest = 1e10f;
          for (int c = 0; c < kClusters; c++)
          {
            if (mCstr[c] < 0.01f) continue;
            float dx = spx - mCx[c];
            float dy = spy - mCy[c];
            float dz = spz - mCz[c];
            float d = dx * dx + dy * dy + dz * dz;
            if (d < nearest)
            {
              secondNearest = nearest;
              nearest = d;
            }
            else if (d < secondNearest)
            {
              secondNearest = d;
            }
          }

          // Voronoi edge brightness
          float edgeRatio = (secondNearest > 0.0001f) ? nearest / secondNearest : 0.0f;
          if (edgeRatio < 0.75f) continue; // skip deep cell interiors

          // Depth shading on shell
          float depth = 0.3f + 0.7f * ((tz + shellR) / (2.0f * shellR));
          if (depth < 0.15f) depth = 0.15f;
          if (depth > 1.0f) depth = 1.0f;

          float edgeBright = (edgeRatio - 0.75f) / 0.25f; // 0-1
          int gray = 2 + (int)(edgeBright * 6.0f * depth);
          if (gray > 10) gray = 10;

          int px = left + (int)(snx * (float)w);
          int py = bot + (int)(sny * (float)h);
          if (px >= left && px < left + w && py >= bot && py < bot + h)
            fb.pixel(gray, px, py);
        }
      }
    }
  };

} // namespace stolmine
