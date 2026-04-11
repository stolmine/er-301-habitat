#pragma once

#include <od/graphics/Graphic.h>
#include "Helicase.h"
#include <math.h>
#include <string.h>

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
    uint8_t mPixels[64 * 64];
    bool mInitialized = false;
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

      // Auto-scale
      float expandRate = 0.5f;
      float contractRate = 0.01f;

      // Plot carrier output (X) vs modulator output (Y) -- Lissajous
      for (int i = 0; i < 256; i++)
      {
        float x = mpHelicase->getOutputSample(i);
        float y = mpHelicase->getModulatorSample(i);

        if (x < mMinX) mMinX += (x - mMinX) * expandRate;
        if (x > mMaxX) mMaxX += (x - mMaxX) * expandRate;
        if (y < mMinY) mMinY += (y - mMinY) * expandRate;
        if (y > mMaxY) mMaxY += (y - mMaxY) * expandRate;

        mMinX += (x - mMinX) * contractRate;
        mMaxX += (x - mMaxX) * contractRate;
        mMinY += (y - mMinY) * contractRate;
        mMaxY += (y - mMaxY) * contractRate;

        float rangeX = mMaxX - mMinX;
        float rangeY = mMaxY - mMinY;
        if (rangeX < 0.01f) rangeX = 0.01f;
        if (rangeY < 0.01f) rangeY = 0.01f;

        float nx = (x - (mMinX + mMaxX) * 0.5f) / (rangeX * 0.5f);
        float ny = (y - (mMinY + mMaxY) * 0.5f) / (rangeY * 0.5f);

        int px = (int)((nx * 0.45f + 0.5f) * 63.0f);
        int py = (int)((ny * 0.45f + 0.5f) * 63.0f);

        if (px >= 0 && px < 64 && py >= 0 && py < 64)
        {
          int brightness = 6 + (i * 6 / 256);
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

      // Crosshair (dim)
      int cx = left + w / 2;
      int cy = bot + h / 2;
      for (int px = 0; px < w; px += 4)
        fb.pixel(GRAY2, left + px, cy);
      for (int py = 0; py < h; py += 4)
        fb.pixel(GRAY2, cx, bot + py);
    }
  };

} // namespace stolmine
