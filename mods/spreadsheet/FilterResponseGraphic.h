#pragma once

#include <od/graphics/Graphic.h>
#include <Filterbank.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace stolmine
{
  // 72-entry cos/sin LUT at a = 2*pi*i/72 - pi/2.
  // Used via lutCos/lutSin helpers with linear interpolation for arbitrary
  // radians. Replaces runtime sinf/cosf calls which miscompute on the current
  // toolchain when invoked from a package .so on am335x.
  static const float kLutCos[72] = {
    +0.00000000f, +0.08715574f, +0.17364818f, +0.25881905f, +0.34202014f,
    +0.42261826f, +0.50000000f, +0.57357644f, +0.64278761f, +0.70710678f,
    +0.76604444f, +0.81915204f, +0.86602540f, +0.90630779f, +0.93969262f,
    +0.96592583f, +0.98480775f, +0.99619470f, +1.00000000f, +0.99619470f,
    +0.98480775f, +0.96592583f, +0.93969262f, +0.90630779f, +0.86602540f,
    +0.81915204f, +0.76604444f, +0.70710678f, +0.64278761f, +0.57357644f,
    +0.50000000f, +0.42261826f, +0.34202014f, +0.25881905f, +0.17364818f,
    +0.08715574f, +0.00000000f, -0.08715574f, -0.17364818f, -0.25881905f,
    -0.34202014f, -0.42261826f, -0.50000000f, -0.57357644f, -0.64278761f,
    -0.70710678f, -0.76604444f, -0.81915204f, -0.86602540f, -0.90630779f,
    -0.93969262f, -0.96592583f, -0.98480775f, -0.99619470f, -1.00000000f,
    -0.99619470f, -0.98480775f, -0.96592583f, -0.93969262f, -0.90630779f,
    -0.86602540f, -0.81915204f, -0.76604444f, -0.70710678f, -0.64278761f,
    -0.57357644f, -0.50000000f, -0.42261826f, -0.34202014f, -0.25881905f,
    -0.17364818f, -0.08715574f
  };
  static const float kLutSin[72] = {
    -1.00000000f, -0.99619470f, -0.98480775f, -0.96592583f, -0.93969262f,
    -0.90630779f, -0.86602540f, -0.81915204f, -0.76604444f, -0.70710678f,
    -0.64278761f, -0.57357644f, -0.50000000f, -0.42261826f, -0.34202014f,
    -0.25881905f, -0.17364818f, -0.08715574f, +0.00000000f, +0.08715574f,
    +0.17364818f, +0.25881905f, +0.34202014f, +0.42261826f, +0.50000000f,
    +0.57357644f, +0.64278761f, +0.70710678f, +0.76604444f, +0.81915204f,
    +0.86602540f, +0.90630779f, +0.93969262f, +0.96592583f, +0.98480775f,
    +0.99619470f, +1.00000000f, +0.99619470f, +0.98480775f, +0.96592583f,
    +0.93969262f, +0.90630779f, +0.86602540f, +0.81915204f, +0.76604444f,
    +0.70710678f, +0.64278761f, +0.57357644f, +0.50000000f, +0.42261826f,
    +0.34202014f, +0.25881905f, +0.17364818f, +0.08715574f, +0.00000000f,
    -0.08715574f, -0.17364818f, -0.25881905f, -0.34202014f, -0.42261826f,
    -0.50000000f, -0.57357644f, -0.64278761f, -0.70710678f, -0.76604444f,
    -0.81915204f, -0.86602540f, -0.90630779f, -0.93969262f, -0.96592583f,
    -0.98480775f, -0.99619470f
  };

  // Arbitrary-angle lookup with linear interpolation. Wraps via a large
  // positive bias (72000 periods) so negative angles truncate safely without
  // floorf (also libm).
  static inline float lutCos(float rad)
  {
    const float scale = 72.0f / (2.0f * M_PI);
    const float bias = M_PI * 0.5f * scale + 72.0f * 1000.0f;
    float t = rad * scale + bias;
    int ii = (int)t;
    float frac = t - (float)ii;
    int i = ii % 72;
    int next = (i + 1) % 72;
    return kLutCos[i] + (kLutCos[next] - kLutCos[i]) * frac;
  }

  static inline float lutSin(float rad)
  {
    const float scale = 72.0f / (2.0f * M_PI);
    const float bias = M_PI * 0.5f * scale + 72.0f * 1000.0f;
    float t = rad * scale + bias;
    int ii = (int)t;
    float frac = t - (float)ii;
    int i = ii % 72;
    int next = (i + 1) % 72;
    return kLutSin[i] + (kLutSin[next] - kLutSin[i]) * frac;
  }

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

      fb.circle(GRAY3, cx, cy, maxR);

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

      float baseR = 0.35f * (float)maxR;
      float bumpRange = (float)maxR - baseR;
      float invMax = 1.0f / maxVal;

      float rotateVal = mpFB->getRotate();
      float rotateOffset = 2.0f * M_PI * rotateVal / (float)bandCount;

      float freqLog[16];
      float logLo = 1e10f, logHi = -1e10f;
      for (int i = 0; i < bandCount; i++)
      {
        float hz = mpFB->getBandCurrentFreq(i);
        freqLog[i] = logf(CLAMP(20.0f, 20000.0f, hz));
        if (freqLog[i] < logLo) logLo = freqLog[i];
        if (freqLog[i] > logHi) logHi = freqLog[i];
      }
      float logRange = logHi - logLo;
      if (logRange < 0.1f) logRange = 0.1f;
      float avgSpacing = (bandCount > 1) ? logRange / (float)(bandCount - 1) : logRange;
      float totalRange = logRange + avgSpacing;
      float logPad = avgSpacing * 0.5f;
      logLo -= logPad;
      logRange = totalRange;

      float bandAngles[16];
      float bandBump[16];
      for (int i = 0; i < bandCount; i++)
      {
        bandAngles[i] = 2.0f * M_PI * (freqLog[i] - logLo) / logRange - M_PI * 0.5f + rotateOffset;
        bandBump[i] = values[i] * invMax * bumpRange;
      }

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

      float sampleR[72];
      int px[72], py[72];
      float bumpScale = bumpRange / maxBump;
      for (int step = 0; step < kSteps; step++)
      {
        sampleR[step] = baseR + rawBumps[step] * bumpScale;
        // Perimeter uses LUT directly — step-to-index is identity.
        px[step] = cx + (int)(kLutCos[step] * sampleR[step]);
        py[step] = cy + (int)(kLutSin[step] * sampleR[step]);
      }

      // Gradient radial fill: all 16 gray levels, Q controls brightness
      float macroQ = mpFB->getMacroQ();
      for (int step = 0; step < kSteps; step++)
      {
        float ca = kLutCos[step], sa = kLutSin[step];
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
          float t = (float)g / (float)(numBands - 1);
          int color = 1 + (int)(t * macroQ * 14.0f);
          if (color < 1) color = 1;
          if (color > 15) color = 15;
          fb.line(color, x0, y0, x1, y1);
        }
      }

      for (int i = 0; i < kSteps; i++)
      {
        int next = (i + 1) % kSteps;
        fb.line(WHITE, px[i], py[i], px[next], py[next]);
      }

      // Band spokes — angles are arbitrary radians, use LUT helpers.
      for (int i = 0; i < bandCount; i++)
      {
        float ca = lutCos(bandAngles[i]);
        float sa = lutSin(bandAngles[i]);
        int edgeX = cx + (int)(ca * (float)maxR);
        int edgeY = cy + (int)(sa * (float)maxR);
        if (i == mSelectedBand)
        {
          fb.line(GRAY3, cx, cy, edgeX, edgeY);
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
