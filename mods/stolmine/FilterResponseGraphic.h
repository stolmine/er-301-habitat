#pragma once

#include <od/graphics/Graphic.h>
#include <Filterbank.h>
#include <math.h>

namespace stolmine
{
  class FilterResponseGraphic : public od::Graphic
  {
  public:
    FilterResponseGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~FilterResponseGraphic()
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

      // Right boundary
      fb.vline(GRAY5, mWorldLeft + mWidth - 1, mWorldBottom, mWorldBottom + mHeight - 1, 0);

      // Draw horizontal lines at each band's frequency position
      for (int i = 0; i < bandCount; i++)
      {
        float hz = mpFB->getBandFreq(i);
        int y = freqToY(hz);
        if (i == mSelectedBand)
          fb.hline(GRAY7, mWorldLeft, mWorldLeft + mWidth - 2, y, 0);
        else
          fb.hline(GRAY3, mWorldLeft, mWorldLeft + mWidth - 2, y, 0);
      }

      // Two-pass response rendering: find max, then draw normalized
      float responses[64];
      float maxResponse = 0.0001f;
      int h = MIN(mHeight, 64);
      for (int py = 0; py < h; py++)
      {
        float normalizedFreq = (float)py / (float)(h - 1);
        responses[py] = mpFB->evaluateResponse(normalizedFreq);
        if (responses[py] > maxResponse)
          maxResponse = responses[py];
      }

      for (int py = 0; py < h; py++)
      {
        float normalized = responses[py] / maxResponse;
        int barWidth = (int)(normalized * (float)(mWidth - 2));
        barWidth = CLAMP(0, mWidth - 2, barWidth);

        int y = mWorldBottom + py;
        if (barWidth > 0)
        {
          fb.hline(WHITE, mWorldLeft, mWorldLeft + barWidth - 1, y, 0);
        }
      }

      // Highlight selected band with brighter marker
      if (mSelectedBand >= 0 && mSelectedBand < bandCount)
      {
        float hz = mpFB->getBandFreq(mSelectedBand);
        int y = freqToY(hz);
        // Small marker triangle on the right edge
        fb.line(WHITE, mWorldLeft + mWidth - 4, y - 2,
                mWorldLeft + mWidth - 2, y);
        fb.line(WHITE, mWorldLeft + mWidth - 2, y,
                mWorldLeft + mWidth - 4, y + 2);
      }
    }

    int freqToY(float hz)
    {
      // Log map: 20Hz -> bottom, 20kHz -> top
      float logMin = 2.9957f; // log(20)
      float logMax = 9.9035f; // log(20000)
      float logHz = logf(CLAMP(20.0f, 20000.0f, hz));
      float normalized = (logHz - logMin) / (logMax - logMin);
      return mWorldBottom + (int)(normalized * (float)(mHeight - 1));
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

  private:
    Filterbank *mpFB = 0;
    int mSelectedBand = 0;
  };

} // namespace stolmine
