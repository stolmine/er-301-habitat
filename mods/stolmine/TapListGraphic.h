#pragma once

#include <od/graphics/Graphic.h>
#include <od/objects/Parameter.h>
#include <MultitapDelay.h>
#include <string.h>
#include <stdio.h>

namespace stolmine
{
  class TapListGraphic : public od::Graphic
  {
  public:
    TapListGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~TapListGraphic()
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

      const int rowHeight = 10;
      const int visibleRows = mHeight / rowHeight;

      if (mSelectedTap < mScrollOffset)
        mScrollOffset = mSelectedTap;
      if (mSelectedTap >= mScrollOffset + visibleRows)
        mScrollOffset = mSelectedTap - visibleRows + 1;
      if (mScrollOffset < 0)
        mScrollOffset = 0;

      for (int r = 0; r < visibleRows; r++)
      {
        int tap = mScrollOffset + r;
        if (tap >= tapCount)
          break;

        int y = mWorldBottom + mHeight - (r + 1) * rowHeight;

        if (tap == mSelectedTap)
          fb.box(WHITE, mWorldLeft, y, mWorldLeft + mWidth - 1, y + rowHeight - 1);

        // Tap number
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d", tap + 1);
        fb.text(GRAY7, mWorldLeft + 2, y + 1, buf, 10);

        // Per-row value label
        if (mShowCutoff)
        {
          float hz = (tap == mSelectedTap && mpEditLevel)
                         ? mpEditLevel->value()
                         : mpDelay->getFilterCutoff(tap);
          formatFreq(buf, sizeof(buf), hz);
          fb.text(WHITE, mWorldLeft + 18, y + 1, buf, 10);
        }
        else
        {
          float level = (tap == mSelectedTap && mpEditLevel)
                            ? mpEditLevel->value()
                            : mpDelay->getTapLevel(tap);
          if (level < 0.001f)
          {
            fb.text(GRAY5, mWorldLeft + 18, y + 1, "off", 10);
          }
          else
          {
            snprintf(buf, sizeof(buf), "%3d", (int)(level * 100.0f));
            fb.text(WHITE, mWorldLeft + 18, y + 1, buf, 10);
          }
        }
      }

      // Scrollbar
      if (tapCount > visibleRows)
      {
        int barHeight = MAX(2, mHeight * visibleRows / tapCount);
        int barY = mWorldBottom + mHeight - barHeight
                   - (mHeight - barHeight) * mScrollOffset / (tapCount - visibleRows);
        fb.vline(GRAY5, mWorldLeft + mWidth - 1, barY, barY + barHeight - 1, 0);
      }
    }

    static void formatTime(char *buf, int bufSize, float t)
    {
      // t is 0-1 (position in master window). Show as percentage or ms.
      int ms = (int)(t * 1000.0f);
      if (ms < 100)
        snprintf(buf, bufSize, ".%02d", ms);
      else if (ms < 1000)
        snprintf(buf, bufSize, "%3d", ms);
      else
        snprintf(buf, bufSize, "%ds", ms / 1000);
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
    int getSelectedTap() { return mSelectedTap; }
    void setEditParam(od::Parameter *editLevel) { mpEditLevel = editLevel; }
    void setShowCutoff(bool show) { mShowCutoff = show; }

    static void formatFreq(char *buf, int bufSize, float hz)
    {
      if (hz < 1000.0f)
        snprintf(buf, bufSize, "%3.0f", hz);
      else if (hz < 10000.0f)
      {
        int k = (int)(hz / 1000.0f);
        int frac = ((int)(hz / 100.0f)) % 10;
        snprintf(buf, bufSize, "%dk%d", k, frac);
      }
      else
        snprintf(buf, bufSize, "%dk", (int)(hz / 1000.0f));
    }

  private:
    MultitapDelay *mpDelay = 0;
    od::Parameter *mpEditLevel = 0;
    int mSelectedTap = 0;
    int mScrollOffset = 0;
    bool mShowCutoff = false;
  };

} // namespace stolmine
