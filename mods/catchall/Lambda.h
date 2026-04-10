#pragma once

#include <od/objects/Object.h>
#include <od/graphics/Graphic.h>
#include <od/config.h>
#include <math.h>

namespace stolmine
{

  static const int kLambdaWaveSize = 256;
  static const int kLambdaFrames = 24;
  static const int kLambdaMaxSections = 4;

  class Lambda : public od::Object
  {
  public:
    Lambda();
    virtual ~Lambda();

    // SWIG-visible
    float getWaveformSample(int idx);
    int getCurrentSeed();
    float getEnvelope();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};

    od::Parameter mSeed{"Seed", 0.0f};
    od::Parameter mScan{"Scan", 0.0f};
    od::Parameter mFundamental{"Fundamental", 110.0f};
    od::Parameter mCutoff{"Cutoff", 1000.0f};
    od::Parameter mLevel{"Level", 0.5f};

  private:
    struct Internal;
    Internal *mpInternal;
#endif
  };

  class LambdaWaveGraphic : public od::Graphic
  {
  public:
    LambdaWaveGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpLambda(0) {}

    virtual ~LambdaWaveGraphic()
    {
      if (mpLambda)
        mpLambda->release();
    }

    void follow(Lambda *p)
    {
      if (mpLambda)
        mpLambda->release();
      mpLambda = p;
      if (mpLambda)
        mpLambda->attach();
    }

  private:
    Lambda *mpLambda;
    float mEnvSlew = 0.0f;

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth;
      int h = mHeight;
      int left = mWorldLeft;
      int bot = mWorldBottom;

      fb.fill(BLACK, left, bot, left + w - 1, bot + h - 1);

      if (!mpLambda)
        return;

      float env = mpLambda->getEnvelope();
      float envTarget = env / (env + 0.1f);
      mEnvSlew += (envTarget - mEnvSlew) * 0.15f;

      // Brightness scales with audio level
      int baseBright = 4 + (int)(mEnvSlew * 8.0f);
      if (baseBright > 12) baseBright = 12;

      // Draw waveform
      int prevY = -1;
      for (int px = 0; px < w; px++)
      {
        // Map pixel to wavetable sample
        int idx = (px * kLambdaWaveSize) / w;
        if (idx >= kLambdaWaveSize) idx = kLambdaWaveSize - 1;
        float sample = mpLambda->getWaveformSample(idx);

        // Map sample to screen Y (-1..1 -> bot..top)
        int cy = bot + h / 2;
        int py = cy + (int)(sample * (float)(h / 2 - 2));
        if (py < bot) py = bot;
        if (py >= bot + h) py = bot + h - 1;

        // Draw vertical line between previous and current Y for solid fill
        if (prevY >= 0)
        {
          int y0 = prevY < py ? prevY : py;
          int y1 = prevY > py ? prevY : py;
          for (int y = y0; y <= y1; y++)
            fb.pixel(baseBright, left + px, y);
        }
        else
        {
          fb.pixel(baseBright, left + px, py);
        }
        prevY = py;
      }

      // Center line (dim)
      int cy = bot + h / 2;
      for (int px = 0; px < w; px += 2)
        fb.pixel(2, left + px, cy);

      // Outer border
      fb.box(WHITE, left, bot, left + w - 1, bot + h - 1);
    }
  };

} // namespace stolmine
