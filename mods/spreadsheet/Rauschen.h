#pragma once

#include <od/objects/Object.h>
#include <od/graphics/Graphic.h>
#include <od/config.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  class Rauschen : public od::Object
  {
  public:
    Rauschen();
    virtual ~Rauschen();

    // SWIG-visible
    float getOutputSample(int idx);

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};

    od::Parameter mAlgorithm{"Algorithm", 0.0f};
    od::Parameter mParamX{"ParamX", 0.5f};
    od::Parameter mParamY{"ParamY", 0.5f};
    od::Parameter mFilterFreq{"FilterFreq", 1000.0f};
    od::Parameter mFilterQ{"FilterQ", 0.5f};
    od::Parameter mFilterMorph{"FilterMorph", 0.0f};
    od::Parameter mLevel{"Level", 0.5f};

  private:
    struct Internal;
    Internal *mpInternal;
#endif
  };

  // Phase space plot: x[n] vs x[n-1]
  class PhaseSpaceGraphic : public od::Graphic
  {
  public:
    PhaseSpaceGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpRauschen(0) {}

    virtual ~PhaseSpaceGraphic()
    {
      if (mpRauschen)
        mpRauschen->release();
    }

    void follow(Rauschen *p)
    {
      if (mpRauschen)
        mpRauschen->release();
      mpRauschen = p;
      if (mpRauschen)
        mpRauschen->attach();
    }

  private:
    Rauschen *mpRauschen;

    // Persistence buffer for phosphor decay
    static const int kMaxW = 64;
    static const int kMaxH = 64;
    uint8_t mPixels[kMaxW * kMaxH];
    bool mCleared = false;

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth < kMaxW ? mWidth : kMaxW;
      int h = mHeight < kMaxH ? mHeight : kMaxH;

      if (!mCleared)
      {
        memset(mPixels, 0, sizeof(mPixels));
        mCleared = true;
      }

      // Fade existing pixels
      for (int i = 0; i < w * h; i++)
      {
        if (mPixels[i] > 0)
          mPixels[i]--;
      }

      // Plot new samples from ring buffer
      if (mpRauschen)
      {
        for (int i = 0; i < 255; i++)
        {
          float s0 = mpRauschen->getOutputSample(i);
          float s1 = mpRauschen->getOutputSample(i + 1);

          // Map [-1, 1] to pixel coordinates
          int px = (int)((s0 * 0.5f + 0.5f) * (float)(w - 1));
          int py = (int)((s1 * 0.5f + 0.5f) * (float)(h - 1));
          if (px < 0) px = 0;
          if (px >= w) px = w - 1;
          if (py < 0) py = 0;
          if (py >= h) py = h - 1;

          int brightness = mPixels[py * w + px] + 4;
          if (brightness > 12) brightness = 12;
          mPixels[py * w + px] = (uint8_t)brightness;
        }
      }

      // Render to framebuffer
      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);

      for (int y = 0; y < h; y++)
      {
        for (int x = 0; x < w; x++)
        {
          uint8_t v = mPixels[y * w + x];
          if (v > 0)
            fb.pixel(v, mWorldLeft + x, mWorldBottom + y);
        }
      }
    }
  };

} // namespace stolmine
