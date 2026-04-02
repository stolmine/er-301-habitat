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

      // Sample radius around circle: base + band bumps at frequency positions
      float baseR = 0.35f * (float)maxR;
      float bumpRange = (float)maxR - baseR;
      float invMax = 1.0f / maxVal;
      float logMin = 2.9957f;  // log(20)
      float logMax = 9.9035f;  // log(20000)
      float logRange = logMax - logMin;

      // Rotate offset: whole graphic spins with rotate parameter
      float rotateVal = mpFB->getRotate();
      float rotateOffset = 2.0f * M_PI * rotateVal / (float)bandCount;

      // Band angles from current (slewed) frequency + rotation
      float bandAngles[16];
      float bandBump[16];
      for (int i = 0; i < bandCount; i++)
      {
        float hz = mpFB->getBandCurrentFreq(i);
        float logHz = logf(CLAMP(20.0f, 20000.0f, hz));
        bandAngles[i] = 2.0f * M_PI * (logHz - logMin) / logRange - M_PI * 0.5f + rotateOffset;
        bandBump[i] = values[i] * invMax * bumpRange;
      }

      // Sample at N points, radius = base + Gaussian bumps from all bands
      static const int kSteps = 72;
      float sigma = 0.2f;
      float invSigma2 = 1.0f / (2.0f * sigma * sigma);
      float rawBumps[72];
      float maxBump = 0.0001f;
      for (int step = 0; step < kSteps; step++)
      {
        float a = 2.0f * M_PI * (float)step / (float)kSteps - M_PI * 0.5f;
        float bump = 0.0f;
        for (int i = 0; i < bandCount; i++)
        {
          float da = a - bandAngles[i];
          while (da > M_PI) da -= 2.0f * M_PI;
          while (da < -M_PI) da += 2.0f * M_PI;
          bump += bandBump[i] * expf(-da * da * invSigma2);
        }
        rawBumps[step] = bump;
        if (bump > maxBump) maxBump = bump;
      }

      // Normalize so max bump exactly reaches maxR
      float sampleR[72];
      int px[72], py[72];
      float bumpScale = bumpRange / maxBump;
      for (int step = 0; step < kSteps; step++)
      {
        float a = 2.0f * M_PI * (float)step / (float)kSteps - M_PI * 0.5f;
        sampleR[step] = baseR + rawBumps[step] * bumpScale;
        px[step] = cx + (int)(cosf(a) * sampleR[step]);
        py[step] = cy + (int)(sinf(a) * sampleR[step]);
      }

      // Gradient fill: all 16 gray levels, Q controls brightness
      float macroQ = mpFB->getMacroQ();
      for (int step = 0; step < kSteps; step++)
      {
        float a = 2.0f * M_PI * (float)step / (float)kSteps - M_PI * 0.5f;
        float ca = cosf(a), sa = sinf(a);
        float range = sampleR[step] - baseR;
        if (range < 1.0f) continue;
        int numBands = (int)(range);
        if (numBands < 2) numBands = 2;
        if (numBands > 16) numBands = 16;
        for (int g = 0; g < numBands; g++)
        {
          float t0 = (float)g / (float)numBands;
          float t1 = (float)(g + 1) / (float)numBands;
          float r0 = baseR + range * t0;
          float r1 = baseR + range * t1;
          int x0 = cx + (int)(ca * r0);
          int y0 = cy + (int)(sa * r0);
          int x1 = cx + (int)(ca * r1);
          int y1 = cy + (int)(sa * r1);
          // Q scales brightness: low Q = dim gradient, high Q = bright
          float t = (float)g / (float)(numBands - 1);
          int color = 1 + (int)(t * macroQ * 14.0f);
          if (color < 1) color = 1;
          if (color > 15) color = 15;
          fb.line(color, x0, y0, x1, y1);
        }
      }

      // Draw smooth closed curve on top
      for (int i = 0; i < kSteps; i++)
      {
        int next = (i + 1) % kSteps;
        fb.line(WHITE, px[i], py[i], px[next], py[next]);
      }

      // Band spokes at frequency positions
      for (int i = 0; i < bandCount; i++)
      {
        float ca = cosf(bandAngles[i]), sa = sinf(bandAngles[i]);
        int edgeX = cx + (int)(ca * (float)maxR);
        int edgeY = cy + (int)(sa * (float)maxR);
        if (i == mSelectedBand)
        {
          fb.line(WHITE, cx, cy, edgeX, edgeY);
          // Dot on the curve at this band's angle
          int dotX = cx + (int)(ca * (baseR + bandBump[i]));
          int dotY = cy + (int)(sa * (baseR + bandBump[i]));
          fb.fillCircle(WHITE, dotX, dotY, 2);
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
