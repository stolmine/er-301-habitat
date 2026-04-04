#pragma once

#include <od/graphics/Graphic.h>
#include <MultitapDelay.h>
#include <stdio.h>
#include <math.h>

namespace stolmine
{

  static const char *kXformNames[] = {
      "all", "taps", "dly", "flt",
      "lvl", "pan", "pch", "cut", "Q", "typ",
      "time", "fdbk", "tone", "skew", "grn", "drft", "rev", "stk", "grd", "cnt",
      "rst"};
  static const int kXformNameCount = 21;

  class DelayInfoGraphic : public od::Graphic
  {
  public:
    DelayInfoGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~DelayInfoGraphic()
    {
      if (mpDelay)
        mpDelay->release();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpDelay)
        return;

      int left = mWorldLeft + 2;
      int right = mWorldLeft + mWidth - 2;
      int top = mWorldBottom + mHeight;
      int w = right - left;
      char buf[16];

      int tapCount = mpDelay->getTapCount();
      float masterTime = mpDelay->mMasterTime.value();
      int gridExp = (int)(mpDelay->mGrid.value() + 0.5f);
      if (gridExp < 0) gridExp = 0;
      if (gridExp > 4) gridExp = 4;
      int grid = 1 << gridExp;
      int stackExp = (int)(mpDelay->mStack.value() + 0.5f);
      if (stackExp < 0) stackExp = 0;
      if (stackExp > 4) stackExp = 4;
      int stack = 1 << stackExp;
      float grain = mpDelay->mGrainSize.value();
      int xformIdx = (int)(mpDelay->mXformTarget.value() + 0.5f);
      if (xformIdx < 0) xformIdx = 0;
      if (xformIdx >= kXformNameCount) xformIdx = kXformNameCount - 1;

      // Row 1: tap count with bar
      snprintf(buf, sizeof(buf), "taps %d", tapCount);
      fb.text(WHITE, left, top - 11, buf, 10);
      {
        int barY = top - 14;
        fb.fill(GRAY3, left, barY, w, 2);
        fb.fill(WHITE, left, barY, w * tapCount / kMaxTaps, 2);
      }

      // Row 2: time
      if (masterTime >= 1.0f)
        snprintf(buf, sizeof(buf), "t %.2fs", masterTime);
      else
        snprintf(buf, sizeof(buf), "t %dms", (int)(masterTime * 1000.0f + 0.5f));
      fb.text(GRAY13, left, top - 25, buf, 10);

      // Row 3: grid / stack
      snprintf(buf, sizeof(buf), "g %d  s %d", grid, stack);
      fb.text(GRAY13, left, top - 36, buf, 10);

      // Row 4: grain
      snprintf(buf, sizeof(buf), "gr %.2f", grain);
      fb.text(GRAY10, left, top - 47, buf, 10);

      // Row 5: xform target
      snprintf(buf, sizeof(buf), "xf %s", kXformNames[xformIdx]);
      fb.text(GRAY10, left, top - 58, buf, 10);
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

  private:
    MultitapDelay *mpDelay = 0;
  };

} // namespace stolmine
