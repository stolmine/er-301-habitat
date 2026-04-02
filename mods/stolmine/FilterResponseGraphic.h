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
      // Gain as base shape, modulated by live filter energy
      float values[16];
      float maxVal = 0.0001f;
      for (int i = 0; i < bandCount; i++)
      {
        float gain = mpFB->getBandGain(i);
        float energy = mpFB->getBandEnergy(i);
        values[i] = gain * (0.3f + 0.7f * energy);
        if (values[i] > maxVal)
          maxVal = values[i];
      }

      // Per-band radii at equal angular spacing
      float radii[16];
      float minR = 0.3f * (float)maxR;
      float rangeR = (float)maxR - minR;
      float invMax = 1.0f / maxVal;
      for (int i = 0; i < bandCount; i++)
      {
        float normalized = values[i] * invMax;
        radii[i] = minR + normalized * rangeR;
      }

      // Gaussian-smoothed closed curve at fixed angular steps
      static const int kSteps = 72;
      float bandSpacing = 2.0f * M_PI / (float)bandCount;
      float sigma = bandSpacing * 0.6f;
      float invSigma2 = 1.0f / (2.0f * sigma * sigma);
      int prevX = -1, prevY = -1;
      int firstX = -1, firstY = -1;
      int px[16], py[16]; // store band positions for spokes
      for (int step = 0; step < kSteps; step++)
      {
        float a = 2.0f * M_PI * (float)step / (float)kSteps - M_PI * 0.5f;
        float weightSum = 0.0f;
        float r = 0.0f;
        for (int i = 0; i < bandCount; i++)
        {
          float bandAngle = 2.0f * M_PI * (float)i / (float)bandCount - M_PI * 0.5f;
          float da = a - bandAngle;
          while (da > M_PI) da -= 2.0f * M_PI;
          while (da < -M_PI) da += 2.0f * M_PI;
          float w = expf(-da * da * invSigma2);
          r += radii[i] * w;
          weightSum += w;
        }
        if (weightSum > 0.0001f)
          r /= weightSum;
        if (r < minR) r = minR;

        int x = cx + (int)(cosf(a) * r);
        int y = cy + (int)(sinf(a) * r);
        if (prevX >= 0)
          fb.line(WHITE, prevX, prevY, x, y);
        else
        { firstX = x; firstY = y; }
        prevX = x;
        prevY = y;
      }
      if (prevX >= 0 && firstX >= 0)
        fb.line(WHITE, prevX, prevY, firstX, firstY);

      // Cache band vertex positions for spoke dots
      for (int i = 0; i < bandCount; i++)
      {
        float a = 2.0f * M_PI * (float)i / (float)bandCount - M_PI * 0.5f;
        px[i] = cx + (int)(cosf(a) * radii[i]);
        py[i] = cy + (int)(sinf(a) * radii[i]);
      }

      // Band spokes at equal angles
      for (int i = 0; i < bandCount; i++)
      {
        float a = 2.0f * M_PI * (float)i / (float)bandCount - M_PI * 0.5f;
        int edgeX = cx + (int)(cosf(a) * (float)maxR);
        int edgeY = cy + (int)(sinf(a) * (float)maxR);
        if (i == mSelectedBand)
        {
          fb.line(GRAY7, cx, cy, edgeX, edgeY);
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
