#pragma once

#include <od/graphics/Graphic.h>
#include <Filterbank.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

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
      if (bandCount < 2)
        return;

      int cx = mWorldLeft + mWidth / 2;
      int cy = mWorldBottom + mHeight / 2;
      int maxR = MIN(mWidth, mHeight) / 2 - 3;

      // Reference circle
      fb.circle(GRAY3, cx, cy, maxR);

      // Per-band: blend gain with spectral response for visual variety
      float values[16];
      float maxVal = 0.0001f;
      for (int i = 0; i < bandCount; i++)
      {
        float gain = mpFB->getBandGain(i);
        float resp = mpFB->evaluateResponseAtBand(i);
        values[i] = gain * (0.5f + 0.5f * resp); // gain modulated by response
        if (values[i] > maxVal)
          maxVal = values[i];
      }

      // Compute polygon vertices: angle from frequency, radius from gain
      int px[16], py[16];
      float angles[16];
      float logMin = 2.9957f;  // log(20)
      float logMax = 9.9035f;  // log(20000)
      float logRange = logMax - logMin;
      float minR = 0.4f * (float)maxR;
      float rangeR = (float)maxR - minR;
      float invMax = 1.0f / maxVal;
      for (int i = 0; i < bandCount; i++)
      {
        float hz = mpFB->getBandFreq(i);
        float logHz = logf(CLAMP(20.0f, 20000.0f, hz));
        float freqNorm = (logHz - logMin) / logRange; // 0-1 around circle
        float angle = 2.0f * M_PI * freqNorm - M_PI * 0.5f;
        angles[i] = angle;
        float normalized = values[i] * invMax;
        float r = minR + normalized * rangeR;
        px[i] = cx + (int)(cosf(angle) * r);
        py[i] = cy + (int)(sinf(angle) * r);
      }

      // Draw response polygon (closed)
      for (int i = 0; i < bandCount; i++)
      {
        int next = (i + 1) % bandCount;
        fb.line(WHITE, px[i], py[i], px[next], py[next]);
      }

      // Band spokes at frequency-mapped angles
      for (int i = 0; i < bandCount; i++)
      {
        int edgeX = cx + (int)(cosf(angles[i]) * (float)maxR);
        int edgeY = cy + (int)(sinf(angles[i]) * (float)maxR);
        if (i == mSelectedBand)
        {
          fb.line(GRAY7, cx, cy, edgeX, edgeY);
          // Dot at response point
          fb.fillCircle(WHITE, px[i], py[i], 2);
        }
        else
        {
          fb.line(GRAY3, cx, cy, edgeX, edgeY);
        }
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

  private:
    Filterbank *mpFB = 0;
    int mSelectedBand = 0;
  };

} // namespace stolmine
