#pragma once

#include <od/graphics/Graphic.h>
#include <Larets.h>
#include <stdio.h>

namespace stolmine
{
  class LaretOverviewGraphic : public od::Graphic
  {
  public:
    LaretOverviewGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~LaretOverviewGraphic()
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

      const int barAreaHeight = 24;
      const int scopeAreaHeight = mHeight - barAreaHeight;
      const int barBottom = mWorldBottom;
      const int barTop = barBottom + barAreaHeight;
      const int scopeBottom = barTop;

      // Scope placeholder: horizontal center line in GRAY3
      int scopeMid = scopeBottom + scopeAreaHeight / 2;
      fb.hline(GRAY3, mWorldLeft, mWorldLeft + mWidth - 1, scopeMid);

      // Compute total effective ticks for proportional bar widths
      float totalTicks = 0.0f;
      for (int i = 0; i < stepCount; i++)
        totalTicks += mpLarets->getEffectiveTickCount(i);
      if (totalTicks <= 0.0f)
        return;

      static const char kTypeLetter[] = {
          'O', 'S', 'R', 'B', 'D', 'F', 'P', 'T', 'G', 'X', 'H'};

      int xCursor = mWorldLeft;
      for (int i = 0; i < stepCount; i++)
      {
        float eff = mpLarets->getEffectiveTickCount(i);
        int barW = (int)(eff / totalTicks * (float)mWidth + 0.5f);
        if (barW < 1) barW = 1;
        // Clamp last bar to avoid overflow
        if (i == stepCount - 1)
          barW = mWorldLeft + mWidth - xCursor;

        int barRight = xCursor + barW - 1;
        int barFill = barTop - 1;

        if (i == activeStep)
        {
          fb.fill(WHITE, xCursor, barBottom, barRight, barFill);
        }
        else
        {
          fb.box(GRAY3, xCursor, barBottom, barRight, barFill);
        }

        // Effect type letter centered in bar
        if (barW >= 6)
        {
          int type = mpLarets->getStepType(i);
          if (type < 0) type = 0;
          if (type > 10) type = 10;
          char letter[2] = {kTypeLetter[type], '\0'};
          int letterX = xCursor + barW / 2 - 2;
          int letterY = barBottom + (barAreaHeight - 10) / 2;
          int color = (i == activeStep) ? BLACK : GRAY7;
          fb.text(color, letterX, letterY, letter, 10);
        }

        // Selected step gets an extra highlight box above the bar area
        if (i == mSelectedStep && i != activeStep)
        {
          fb.box(GRAY5, xCursor, barBottom, barRight, barFill);
        }

        xCursor += barW;
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

  private:
    Larets *mpLarets = 0;
    int mSelectedStep = 0;
  };

} // namespace stolmine
