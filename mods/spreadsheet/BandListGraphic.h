#pragma once

#include <od/graphics/Graphic.h>
#include <od/objects/Parameter.h>
#include <Filterbank.h>
#include <string.h>
#include <stdio.h>

namespace stolmine
{
  class BandListGraphic : public od::Graphic
  {
  public:
    BandListGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~BandListGraphic()
    {
      if (mpFB)
        mpFB->release();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpFB)
        return;

      int bandCount = mpFB->getBandCount();
      if (bandCount < 1)
        return;

      const int rowHeight = 10;
      const int visibleRows = mHeight / rowHeight;

      // Keep selected band visible
      if (mSelectedBand < mScrollOffset)
        mScrollOffset = mSelectedBand;
      if (mSelectedBand >= mScrollOffset + visibleRows)
        mScrollOffset = mSelectedBand - visibleRows + 1;
      if (mScrollOffset < 0)
        mScrollOffset = 0;

      for (int r = 0; r < visibleRows; r++)
      {
        int band = mScrollOffset + r;
        if (band >= bandCount)
          break;

        int y = mWorldBottom + mHeight - (r + 1) * rowHeight;

        // Selected band outline
        if (band == mSelectedBand)
        {
          fb.box(mFocused ? WHITE : GRAY5, mWorldLeft, y, mWorldLeft + mWidth - 1, y + rowHeight - 1);
        }

        // Band number
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d", band + 1);
        fb.text(GRAY7, mWorldLeft + 2, y + 1, buf, 10);

        // Frequency label: 3-char format
        float hz = (band == mSelectedBand && mpEditFreq)
                        ? mpEditFreq->value()
                        : mpFB->getBandFreq(band);
        formatFreq(buf, sizeof(buf), hz);
        fb.text(WHITE, mWorldLeft + 18, y + 1, buf, 10);
      }

      // Scrollbar
      if (bandCount > visibleRows)
      {
        int barHeight = MAX(2, mHeight * visibleRows / bandCount);
        int barY = mWorldBottom + mHeight - barHeight
                   - (mHeight - barHeight) * mScrollOffset / (bandCount - visibleRows);
        fb.vline(GRAY5, mWorldLeft + mWidth - 1, barY, barY + barHeight - 1, 0);
      }
    }

    static void formatFreq(char *buf, int bufSize, float hz)
    {
      if (hz < 1000.0f)
      {
        snprintf(buf, bufSize, "%3.0f", hz);
      }
      else if (hz < 10000.0f)
      {
        int k = (int)(hz / 1000.0f);
        int frac = ((int)(hz / 100.0f)) % 10;
        snprintf(buf, bufSize, "%dk%d", k, frac);
      }
      else
      {
        int k = (int)(hz / 1000.0f);
        snprintf(buf, bufSize, "%dk", k);
      }
    }
#endif

    void follow(Filterbank *pFB)
    {
      if (mpFB)
        mpFB->release();
      mpFB = pFB;
      if (mpFB)
        mpFB->attach();
    }

    void setSelectedBand(int band) { mSelectedBand = band; }
    int getSelectedBand() { return mSelectedBand; }
    void setEditParam(od::Parameter *editFreq) { mpEditFreq = editFreq; }
    void setFocused(bool focused) { mFocused = focused; }

  private:
    Filterbank *mpFB = 0;
    od::Parameter *mpEditFreq = 0;
    int mSelectedBand = 0;
    int mScrollOffset = 0;
    bool mFocused = false;
  };

} // namespace stolmine
