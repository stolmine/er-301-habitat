#pragma once

#include <od/graphics/Graphic.h>
#include <Larets.h>
#include <math.h>

namespace stolmine
{
  class LaretOverviewGraphic : public od::Graphic
  {
  public:
    LaretOverviewGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height)
    {
      for (int i = 0; i < 128; i++)
        mSlewShape[i] = 0.0f;
    }

    virtual ~LaretOverviewGraphic()
    {
      if (mpLarets)
        mpLarets->release();
    }

#ifndef SWIGLUA

    static inline float catmullRom(float p0, float p1, float p2, float p3,
                                   float t, float tau)
    {
      float t2 = t * t, t3 = t2 * t;
      float a = -tau * p0 + (2.0f - tau) * p1 + (tau - 2.0f) * p2 + tau * p3;
      float b = 2.0f * tau * p0 + (tau - 3.0f) * p1 + (3.0f - 2.0f * tau) * p2 - tau * p3;
      float c = -tau * p0 + tau * p2;
      float d = p1;
      return a * t3 + b * t2 + c * t + d;
    }

    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpLarets)
        return;

      int w = mWidth, h = mHeight;
      int left = mWorldLeft, bot = mWorldBottom;
      int centerX = left + w / 2;
      int centerY = bot + h / 2;

      // Snapshot ring buffer every 2 frames to reduce flicker
      mUpdateCounter++;
      if (mUpdateCounter >= 2)
      {
        mUpdateCounter = 0;
        for (int i = 0; i < 128; i++)
          mSnapshot[i] = mpLarets->getOutputSample(i);
      }

      // DC blocker
      float dcSum = 0.0f;
      for (int i = 0; i < 128; i++)
        dcSum += mSnapshot[i];
      float dc = dcSum / 128.0f;
      mDcState += (dc - mDcState) * 0.1f;

      // Slew toward snapshot with DC removal
      for (int i = 0; i < 128; i++)
      {
        float s = mSnapshot[i] - mDcState;
        mSlewShape[i] += (s - mSlewShape[i]) * 0.12f;
      }

      // Lazy constant trim rotation
      mTrimOffset += 0.004f;
      if (mTrimOffset > 1.0f) mTrimOffset -= 1.0f;

      // Slow constant rotation of the ring itself
      mRotAngle += 0.008f;
      if (mRotAngle > 6.28318f) mRotAngle -= 6.28318f;

      // Projection parameters
      float minDim = (float)(w < h ? w : h);
      float cx = (float)w * 0.5f;
      float cy = (float)h * 0.5f;
      float baseRadius = minDim * 0.30f;
      float ampScale = minDim * 0.9f;
      float tiltAngle = 0.6f;
      float cosTilt = cosf(tiltAngle), sinTilt = sinf(tiltAngle);
      float cosRot = cosf(mRotAngle), sinRot = sinf(mRotAngle);
      float tau = 0.7f;

      // Trim window: 65% of circumference
      float trimSize = 0.65f;
      float trimStart = mTrimOffset;
      float trimEnd = mTrimOffset + trimSize;

      // Fade zones at edges (10% of window for soft appear/disappear)
      float fadeZone = 0.08f;

      // Precompute all vertices
      static const int kPts = 128;
      static const int kSubdiv = 3;
      static const int kTotal = kPts * kSubdiv;
      int vtxX[kTotal], vtxY[kTotal], vtxGray[kTotal];
      float vtxZ[kTotal];
      bool vtxVisible[kTotal];

      for (int i = 0; i < kPts; i++)
      {
        for (int s = 0; s < kSubdiv; s++)
        {
          int idx = i * kSubdiv + s;
          float frac = (float)s / (float)kSubdiv;
          float pos = ((float)i + frac) / (float)kPts;

          float posRel = pos - trimStart;
          if (posRel < 0.0f) posRel += 1.0f;
          if (posRel > trimSize)
          {
            vtxVisible[idx] = false;
            continue;
          }
          vtxVisible[idx] = true;

          float edgeFade = 1.0f;
          if (posRel < fadeZone)
            edgeFade = posRel / fadeZone;
          else if (posRel > trimSize - fadeZone)
            edgeFade = (trimSize - posRel) / fadeZone;

          int i0 = (i - 1 + kPts) % kPts;
          int i2 = (i + 1) % kPts;
          int i3 = (i + 2) % kPts;
          float val = catmullRom(mSlewShape[i0], mSlewShape[i],
                                 mSlewShape[i2], mSlewShape[i3], frac, tau);

          float angle = pos * 6.28318f;
          float px = baseRadius * cosf(angle);
          float pz = baseRadius * sinf(angle);
          float py = val * ampScale;

          float rx = px * cosRot - pz * sinRot;
          float rz = px * sinRot + pz * cosRot;

          float ty = py * cosTilt - rz * sinTilt;
          float tz = py * sinTilt + rz * cosTilt;

          vtxX[idx] = left + (int)(cx + rx);
          vtxY[idx] = bot + (int)(cy + ty);
          vtxZ[idx] = tz;

          float depth = (tz + baseRadius + ampScale) / (2.0f * (baseRadius + ampScale));
          if (depth < 0.0f) depth = 0.0f;
          if (depth > 1.0f) depth = 1.0f;
          int gray = 3 + (int)(depth * edgeFade * 10.0f);
          if (gray > 13) gray = 13;
          if (gray < 1) gray = 1;
          vtxGray[idx] = gray;
        }
      }

      // Single-pass draw (matches Helicase orbital pattern)
      int prevSx = -1, prevSy = -1;
      for (int idx = 0; idx < kTotal; idx++)
      {
        if (!vtxVisible[idx])
        {
          prevSx = -1;
          continue;
        }
        int sx = vtxX[idx], sy = vtxY[idx];
        if (prevSx >= 0 &&
            sx >= left && sx < left + w && sy >= bot && sy < bot + h &&
            prevSx >= left && prevSx < left + w && prevSy >= bot && prevSy < bot + h)
        {
          fb.line(vtxGray[idx], prevSx, prevSy, sx, sy);
        }
        prevSx = sx;
        prevSy = sy;
      }
    }
#endif

    void follow(Larets *p)
    {
      if (mpLarets) mpLarets->release();
      mpLarets = p;
      if (mpLarets) mpLarets->attach();
    }

    void setSelectedStep(int step) { mSelectedStep = step; }
    int getSelectedStep() { return mSelectedStep; }

  private:
    Larets *mpLarets = 0;
    int mSelectedStep = 0;
    float mSlewShape[128];
    float mSnapshot[128] = {};
    int mUpdateCounter = 0;
    float mDcState = 0.0f;
    float mTrimOffset = 0.0f;
    float mRotAngle = 0.0f;
  };

} // namespace stolmine
