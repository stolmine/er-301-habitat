#pragma once

#include <od/graphics/Graphic.h>
#include "Helicase.h"
#include <math.h>

namespace stolmine
{

  class HelicaseCurveGraphic : public od::Graphic
  {
  public:
    HelicaseCurveGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpHelicase(0) {}

    virtual ~HelicaseCurveGraphic()
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

    void setDiscParams(float index, float type)
    {
      mDiscIndex = index;
      mDiscType = type;
    }

  private:
    Helicase *mpHelicase;
    float mDiscIndex = 0.0f;
    float mDiscType = 0.0f;

    // Inline disc folder for curve evaluation (matches Helicase.cpp)
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

    float evalFolder(float input)
    {
      int t0 = (int)mDiscType;
      int t1 = t0 + 1;
      if (t0 < 0) t0 = 0;
      if (t1 > 7) t1 = 7;
      if (t0 > 7) t0 = 7;
      float frac = mDiscType - (float)t0;
      float p = (input + 1.0f) * 0.5f;
      float w0 = opl3Wave(p, t0);
      float w1 = opl3Wave(p, t1);
      float folded = w0 + (w1 - w0) * frac;
      return input * (1.0f - mDiscIndex) + folded * mDiscIndex;
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

      int cx = left + w / 2;
      int cy = bot + h / 2;

      // Identity line (dim diagonal)
      fb.line(GRAY3, left, bot, left + w - 1, bot + h - 1);

      // Draw transfer function curve
      int prevY = -1;
      for (int px = 0; px < w; px++)
      {
        // Map pixel to input range -1..1
        float input = (float)px / (float)(w - 1) * 2.0f - 1.0f;
        float output = evalFolder(input);

        // Map output to screen Y
        int py = bot + (int)((output + 1.0f) * 0.5f * (float)(h - 1));
        if (py < bot) py = bot;
        if (py > bot + h - 1) py = bot + h - 1;

        if (prevY >= 0)
          fb.line(WHITE, left + px - 1, prevY, left + px, py);
        prevY = py;
      }

      // Tracing dot: current carrier position on the curve
      float carrPhase = mpHelicase->getCarrierPhase();
      float carrOut = mpHelicase->getCarrierOutput();

      // Input position (carrier sine before folding)
      float carrSine = sinf(carrPhase * 6.28318530718f);
      int dotX = left + (int)((carrSine + 1.0f) * 0.5f * (float)(w - 1));
      int dotY = bot + (int)((carrOut + 1.0f) * 0.5f * (float)(h - 1));
      if (dotX >= left && dotX < left + w && dotY >= bot && dotY < bot + h)
      {
        // Draw 3x3 dot
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++)
          {
            int px = dotX + dx, py = dotY + dy;
            if (px >= left && px < left + w && py >= bot && py < bot + h)
              fb.pixel(WHITE, px, py);
          }
      }

      // Axis lines (dim)
      for (int px = 0; px < w; px += 3)
        fb.pixel(GRAY3, left + px, cy);
      for (int py = 0; py < h; py += 3)
        fb.pixel(GRAY3, cx, bot + py);
    }
  };

} // namespace stolmine
