#pragma once

#include <od/graphics/Graphic.h>
#include "Helicase.h"
#include <math.h>

namespace stolmine
{

  class HelicaseOrbitalGraphic : public od::Graphic
  {
  public:
    HelicaseOrbitalGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpHelicase(0) {}

    virtual ~HelicaseOrbitalGraphic()
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
    float mSlewShape[128];
    bool mSlewInit = false;
    int mUpdateCounter = 0;
    float mSnapshot[256];
    float mDcState = 0.0f; // DC blocker for feedback offset

    static const int kPoints = 128;

    // Catmull-Rom with tension for smooth interpolation between points
    static inline float catmullRom(float p0, float p1, float p2, float p3,
                                   float t, float tau)
    {
      float t2 = t * t;
      float t3 = t2 * t;
      float a = -tau * p0 + (2.0f - tau) * p1 + (tau - 2.0f) * p2 + tau * p3;
      float b = 2.0f * tau * p0 + (tau - 3.0f) * p1 + (3.0f - 2.0f * tau) * p2 - tau * p3;
      float c = -tau * p0 + tau * p2;
      float d = p1;
      return a * t3 + b * t2 + c * t + d;
    }
    static const float kTwoPi;

    // OPL3 waveform (matches Helicase.cpp)
    static inline float opl3Wave(float phase, int shape)
    {
      float s = sinf(phase * 6.28318530718f);
      float p = phase - floorf(phase);
      switch (shape)
      {
      case 0: return s;
      case 1: return s > 0.0f ? s : 0.0f;
      case 2: return fabsf(s);
      case 3: return (p < 0.25f || p > 0.75f) ? fabsf(s) : 0.0f;
      case 4: return ((int)(p * 2.0f) & 1) ? 0.0f : s;
      case 5: return ((int)(p * 2.0f) & 1) ? 0.0f : fabsf(s);
      case 6: return s >= 0.0f ? 1.0f : -1.0f;
      case 7: return 1.0f - p * 2.0f;
      }
      return s;
    }

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth;
      int h = mHeight;
      int left = mWorldLeft;
      int bot = mWorldBottom;

      fb.fill(BLACK, left, bot, left + w - 1, bot + h - 1);

      if (!mpHelicase)
        return;

      if (!mSlewInit)
      {
        for (int i = 0; i < kPoints; i++)
          mSlewShape[i] = 0.0f;
        memset(mSnapshot, 0, sizeof(mSnapshot));
        mSlewInit = true;
      }

      // Continuous rotation
      mRotAngle += 0.015f;
      if (mRotAngle > 6.28318f)
        mRotAngle -= 6.28318f;

      // Snapshot ring buffer every 2 frames to reduce flicker
      mUpdateCounter++;
      if (mUpdateCounter >= 2)
      {
        mUpdateCounter = 0;
        for (int i = 0; i < 256; i++)
          mSnapshot[i] = mpHelicase->getModulatorSample(i);
      }

      float cx = (float)w * 0.5f;
      float cy = (float)h * 0.5f;
      float minDim = (float)(w < h ? w : h);
      float baseRadius = minDim * 0.42f;
      float ampScale = minDim * 0.51f;    // 1.35x taller

      // Oblique tilt: slanting down and to the left
      float tiltAngle = 0.6f;  // ~34 degrees, more oblique
      float cosTilt = cosf(tiltAngle);
      float sinTilt = sinf(tiltAngle);
      float cosRot = cosf(mRotAngle);
      float sinRot = sinf(mRotAngle);

      // DC blocker on snapshot: remove feedback offset
      float dcSum = 0.0f;
      for (int i = 0; i < 256; i++)
        dcSum += mSnapshot[i];
      float dcTarget = dcSum / 256.0f;
      mDcState += (dcTarget - mDcState) * 0.1f;

      // Downsample snapshot to kPoints with averaging, DC removal, heavy slew
      for (int i = 0; i < kPoints; i++)
      {
        int s0 = (i * 256) / kPoints;
        int s1 = ((i + 1) * 256) / kPoints;
        if (s1 > 256) s1 = 256;
        float avg = 0.0f;
        int count = s1 - s0;
        if (count < 1) count = 1;
        for (int j = s0; j < s1; j++)
          avg += mSnapshot[j] - mDcState;
        avg /= (float)count;
        mSlewShape[i] += (avg - mSlewShape[i]) * 0.08f;
      }

      // Project points with Catmull-Rom interpolation between slewed shape points.
      // At each of the kPoints base positions, subdivide into sub-segments for
      // smooth curves that preserve detail at high ratios.
      int prevSx = -1, prevSy = -1;
      static const int kSubdiv = 3; // 3 sub-segments per point = 384 total segments

      for (int i = 0; i < kPoints * kSubdiv; i++)
      {
        int baseIdx = i / kSubdiv;
        float subFrac = (float)(i % kSubdiv) / (float)kSubdiv;

        // Catmull-Rom across 4 neighboring points
        int i0 = (baseIdx - 1 + kPoints) % kPoints;
        int i1 = baseIdx % kPoints;
        int i2 = (baseIdx + 1) % kPoints;
        int i3 = (baseIdx + 2) % kPoints;

        float tau = 0.5f; // standard tension
        float val = catmullRom(mSlewShape[i0], mSlewShape[i1],
                               mSlewShape[i2], mSlewShape[i3],
                               subFrac, tau);

        float angle = ((float)baseIdx + subFrac) / (float)kPoints * 6.28318f;

        float r = baseRadius;
        float px = r * cosf(angle);
        float pz = r * sinf(angle);
        float py = val * ampScale;

        // Rotate around Y axis
        float rx = px * cosRot - pz * sinRot;
        float rz = px * sinRot + pz * cosRot;

        // Isometric tilt
        float ty = py * cosTilt - rz * sinTilt;
        float tz = py * sinTilt + rz * cosTilt;

        int sx = left + (int)(cx + rx);
        int sy = bot + (int)(cy + ty);

        // Depth shading
        float depth = (tz + baseRadius + ampScale) / (2.0f * (baseRadius + ampScale));
        if (depth < 0.0f) depth = 0.0f;
        if (depth > 1.0f) depth = 1.0f;
        int gray = 3 + (int)(depth * 10.0f);
        if (gray > 13) gray = 13;

        if (prevSx >= 0)
        {
          if (sx >= left && sx < left + w && sy >= bot && sy < bot + h &&
              prevSx >= left && prevSx < left + w && prevSy >= bot && prevSy < bot + h)
          {
            fb.line(gray, prevSx, prevSy, sx, sy);
          }
        }
        prevSx = sx;
        prevSy = sy;
      }
    }
  };

} // namespace stolmine
