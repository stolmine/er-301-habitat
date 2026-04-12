#pragma once

#include <od/graphics/Graphic.h>
#include <Larets.h>
#include <stdio.h>

namespace stolmine
{
  class LaretStepListGraphic : public od::Graphic
  {
  public:
    LaretStepListGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~LaretStepListGraphic()
    {
      if (mpLarets)
        mpLarets->release();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpLarets)
        return;

      int stepCount = mpLarets->getStepCount();
      int activeStep = mpLarets->getStep();
      if (stepCount < 1)
        return;

      const int rowHeight = 10;
      const int visibleRows = mHeight / rowHeight;

      if (mSelectedStep < mScrollOffset)
        mScrollOffset = mSelectedStep;
      if (mSelectedStep >= mScrollOffset + visibleRows)
        mScrollOffset = mSelectedStep - visibleRows + 1;
      if (mScrollOffset < 0)
        mScrollOffset = 0;

      static const char *kTypeAbbrev[] = {
          "off", "stt", "rev", "bit", "dec",
          "flt", "pch", "gat", "drv", "shf", "dly", "cmb"};

      for (int r = 0; r < visibleRows; r++)
      {
        int step = mScrollOffset + r;
        if (step >= stepCount)
          break;

        int y = mWorldBottom + mHeight - (r + 1) * rowHeight;

        if (step == activeStep)
        {
          fb.fill(GRAY3, mWorldLeft, y, mWorldLeft + mWidth - 1, y + rowHeight - 1);
        }

        if (step == mSelectedStep)
        {
          fb.box(mFocused ? WHITE : GRAY5, mWorldLeft, y, mWorldLeft + mWidth - 1, y + rowHeight - 1);
        }

        char buf[16];
        snprintf(buf, sizeof(buf), "%02d", step + 1);
        fb.text(GRAY7, mWorldLeft + 2, y + 1, buf, 10);

        int type = mpLarets->getStepType(step);
        if (type < 0) type = 0;
        if (type > 11) type = 11;
        fb.text(WHITE, mWorldLeft + 18, y + 1, kTypeAbbrev[type], 10);
      }

      if (stepCount > visibleRows)
      {
        int barHeight = MAX(2, mHeight * visibleRows / stepCount);
        int barY = mWorldBottom + mHeight - barHeight
                   - (mHeight - barHeight) * mScrollOffset / (stepCount - visibleRows);
        fb.vline(GRAY5, mWorldLeft + mWidth - 1, barY, barY + barHeight - 1, 0);
      }
    }
#endif

    void follow(Larets *p)
    {
      if (mpLarets)
        mpLarets->release();
      mpLarets = p;
      if (mpLarets)
        mpLarets->attach();
    }

    void setSelectedStep(int step) { mSelectedStep = step; }
    int getSelectedStep() { return mSelectedStep; }
    void setFocused(bool focused) { mFocused = focused; }

  private:
    Larets *mpLarets = 0;
    int mSelectedStep = 0;
    int mScrollOffset = 0;
    bool mFocused = false;
  };

} // namespace stolmine
