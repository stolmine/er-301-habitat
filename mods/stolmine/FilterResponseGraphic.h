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
      int w = mWidth - 2;
      int h = mHeight;

      // Right boundary
      fb.vline(GRAY5, mWorldLeft + w + 1, mWorldBottom, mWorldBottom + h - 1, 0);

      // Draw horizontal lines at each band's frequency position
      for (int i = 0; i < bandCount; i++)
      {
        float hz = mpFB->getBandFreq(i);
        int y = freqToY(hz);
        if (i == mSelectedBand)
          fb.hline(GRAY7, mWorldLeft, mWorldLeft + w, y, 0);
        else
          fb.hline(GRAY3, mWorldLeft, mWorldLeft + w, y, 0);
      }

      // Supersample response at 2x resolution, then draw line
      static const int kMaxSamples = 128;
      int sampleCount = MIN(h * 2, kMaxSamples);
      float responses[kMaxSamples];
      float maxResponse = 0.0001f;

      for (int i = 0; i < sampleCount; i++)
      {
        float normalizedFreq = (float)i / (float)(sampleCount - 1);
        responses[i] = mpFB->evaluateResponse(normalizedFreq);
        if (responses[i] > maxResponse)
          maxResponse = responses[i];
      }

      // Draw as connected line with sqrt scaling for dynamic range
      float invMax = 1.0f / maxResponse;
      int prevX = -1, prevY = -1;
      for (int i = 0; i < sampleCount; i++)
      {
        float t = (float)i / (float)(sampleCount - 1);
        float normalized = sqrtf(responses[i] * invMax);
        int x = mWorldLeft + (int)(normalized * (float)w);
        int y = mWorldBottom + (int)(t * (float)(h - 1));
        x = CLAMP(mWorldLeft, mWorldLeft + w, x);

        if (prevX >= 0)
          fb.line(WHITE, prevX, prevY, x, y);

        prevX = x;
        prevY = y;
      }

      // Highlight selected band with brighter marker
      if (mSelectedBand >= 0 && mSelectedBand < bandCount)
      {
        float hz = mpFB->getBandFreq(mSelectedBand);
        int y = freqToY(hz);
        fb.line(WHITE, mWorldLeft + w - 2, y - 2,
                mWorldLeft + w, y);
        fb.line(WHITE, mWorldLeft + w, y,
                mWorldLeft + w - 2, y + 2);
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
