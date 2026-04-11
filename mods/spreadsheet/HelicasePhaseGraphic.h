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
    uint8_t mPixels[64 * 64];
    bool mInitialized = false;
    float mRotAngle = 0.0f;
    float mMinX = -1.0f, mMaxX = 1.0f;
    float mMinY = -1.0f, mMaxY = 1.0f;

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth;
      int h = mHeight;
      int left = mWorldLeft;
      int bot = mWorldBottom;

      if (!mInitialized)
      {
        memset(mPixels, 0, sizeof(mPixels));
        mInitialized = true;
      }

      fb.fill(BLACK, left, bot, left + w - 1, bot + h - 1);

      if (!mpHelicase)
        return;

      // Fade persistence
      for (int i = 0; i < 64 * 64; i++)
        if (mPixels[i] > 0)
          mPixels[i]--;

      // Rotation
      mRotAngle += 0.012f;
      float cosR = cosf(mRotAngle);
      float sinR = sinf(mRotAngle);
      float tiltCos = 0.9f;
      float tiltSin = 0.44f;

      // Auto-scale with smoothing
      float expandRate = 0.5f;
      float contractRate = 0.01f;

      // Plot output samples as 3D phase space: x[n], x[n-1], x[n-2]
      for (int i = 2; i < 256; i++)
      {
        float x = mpHelicase->getOutputSample(i);
        float y = mpHelicase->getOutputSample(i - 1);
        float z = mpHelicase->getOutputSample(i - 2);

        // Auto-scale
        if (x < mMinX) mMinX += (x - mMinX) * expandRate;
        if (x > mMaxX) mMaxX += (x - mMaxX) * expandRate;
        if (y < mMinY) mMinY += (y - mMinY) * expandRate;
        if (y > mMaxY) mMaxY += (y - mMaxY) * expandRate;

        // Contract toward signal
        mMinX += (x - mMinX) * contractRate;
        mMaxX += (x - mMaxX) * contractRate;
        mMinY += (y - mMinY) * contractRate;
        mMaxY += (y - mMaxY) * contractRate;

        float rangeX = mMaxX - mMinX;
        float rangeY = mMaxY - mMinY;
        if (rangeX < 0.01f) rangeX = 0.01f;
        if (rangeY < 0.01f) rangeY = 0.01f;

        // Normalize to -1..1
        float nx = (x - (mMinX + mMaxX) * 0.5f) / (rangeX * 0.5f);
        float ny = (y - (mMinY + mMaxY) * 0.5f) / (rangeY * 0.5f);
        float nz = (z - (mMinX + mMaxX) * 0.5f) / (rangeX * 0.5f);

        // 3D rotation around Y-axis
        float rx = nx * cosR - nz * sinR;
        float ry = ny;
        float rz = nx * sinR + nz * cosR;

        // Tilt around X-axis
        float ty = ry * tiltCos - rz * tiltSin;

        // Project to screen
        int px = (int)((rx * 0.42f + 0.5f) * 63.0f);
        int py = (int)((ty * 0.42f + 0.5f) * 63.0f);

        if (px >= 0 && px < 64 && py >= 0 && py < 64)
        {
          int brightness = 8 + (i * 4 / 256);
          if (brightness > 12) brightness = 12;
          if (mPixels[py * 64 + px] < brightness)
            mPixels[py * 64 + px] = brightness;
        }
      }

      // Render persistence buffer
      for (int py = 0; py < 64 && py < h; py++)
        for (int px = 0; px < 64 && px < w; px++)
          if (mPixels[py * 64 + px] > 0)
            fb.pixel(mPixels[py * 64 + px], left + px, bot + py);
    }
  };

} // namespace stolmine
