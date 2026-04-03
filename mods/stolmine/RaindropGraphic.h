#pragma once

#include <od/graphics/Graphic.h>
#include <MultitapDelay.h>
#include <math.h>

namespace stolmine
{
  class RaindropGraphic : public od::Graphic
  {
  public:
    RaindropGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~RaindropGraphic()
    {
      if (mpDelay)
        mpDelay->release();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpDelay)
        return;

      int tapCount = mpDelay->getTapCount();
      if (tapCount < 1)
        return;

      int left = mWorldLeft;
      int bot = mWorldBottom;
      int w = mWidth;
      int h = mHeight;
      int cy = bot + h / 2;

      // Horizontal baseline (water surface)
      fb.line(GRAY3, left + 2, cy, left + w - 3, cy);

      // Margin for dot placement
      int marginX = 4;
      int marginY = 6;
      int fieldW = w - 2 * marginX;
      int fieldH = h - 2 * marginY;

      for (int t = 0; t < tapCount; t++)
      {
        float time = mpDelay->getTapTime(t);
        float pan = mpDelay->getTapPan(t);
        float level = mpDelay->getTapLevel(t);
        float energy = mpDelay->getTapEnergy(t);

        // Position: time -> x, pan -> y
        int x = left + marginX + (int)(time * (float)fieldW);
        int y = cy + (int)(pan * (float)(fieldH / 2));

        // Clamp to display bounds
        if (x < left + 1) x = left + 1;
        if (x > left + w - 2) x = left + w - 2;
        if (y < bot + 2) y = bot + 2;
        if (y > bot + h - 3) y = bot + h - 3;

        // Ripple rings: energy drives max radius, level drives brightness
        float maxRadius = 2.0f + energy * 12.0f;
        int baseColor = 3 + (int)(level * 8.0f);
        if (baseColor > 15) baseColor = 15;

        // Draw 1-3 expanding ripple rings
        int nRings = 1 + (int)(energy * 2.5f);
        if (nRings > 3) nRings = 3;
        for (int r = nRings; r >= 1; r--)
        {
          float ringR = maxRadius * (float)r / (float)nRings;
          int ri = (int)(ringR + 0.5f);
          if (ri < 1) ri = 1;
          // Outer rings dimmer
          int color = baseColor - (nRings - r) * 3;
          if (color < 1) color = 1;
          fb.circle(color, x, y, ri);
        }

        // Center dot: bright when active
        int dotColor = (t == mSelectedTap) ? WHITE : baseColor;
        fb.fillCircle(dotColor, x, y, 1);
      }
    }
#endif

    void follow(MultitapDelay *pDelay)
    {
      if (mpDelay)
        mpDelay->release();
      mpDelay = pDelay;
      if (mpDelay)
        mpDelay->attach();
    }

    void setSelectedTap(int tap) { mSelectedTap = tap; }

  private:
    MultitapDelay *mpDelay = 0;
    int mSelectedTap = -1;
  };

} // namespace stolmine
