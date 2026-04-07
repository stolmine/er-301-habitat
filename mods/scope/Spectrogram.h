#pragma once

#include <od/objects/Object.h>
#include <od/graphics/Graphic.h>
#include <od/config.h>
#include <math.h>

struct PFFFT_Setup;

namespace scope_unit
{

  class Spectrogram : public od::Object
  {
  public:
    Spectrogram();
    virtual ~Spectrogram();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mInL{"In L"};
    od::Inlet mInR{"In R"};
    od::Outlet mOutL{"Out L"};
    od::Outlet mOutR{"Out R"};
#endif

    // SWIG-visible
    float getFFTPeak(int bin);
    float getFFTRms(int bin);

  private:
    struct Internal;
    Internal *mpInternal;
  };

  // Full-range spectrum display (20-24kHz, log scale)
  class SpectrogramGraphic : public od::Graphic
  {
  public:
    SpectrogramGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpSpec(0) {}

    virtual ~SpectrogramGraphic()
    {
      if (mpSpec)
        mpSpec->release();
    }

    void follow(Spectrogram *p)
    {
      if (mpSpec)
        mpSpec->release();
      mpSpec = p;
      if (mpSpec)
        mpSpec->attach();
    }

  private:
    Spectrogram *mpSpec;

    static const int kMaxWidth = 128;

    static inline float catmullRom(float p0, float p1, float p2, float p3,
                                   float t, float tau)
    {
      float t2 = t * t;
      float t3 = t2 * t;
      float a = -tau * p0 + (2.0f - tau) * p1 + (tau - 2.0f) * p2 + tau * p3;
      float b = 2.0f * tau * p0 + (tau - 3.0f) * p1 + (3.0f - 2.0f * tau) * p2 - tau * p3;
      float c = -tau * p0 + tau * p2;
      float d = p1;
      return a * t3 + b * t2 + c * t + d;
    }

    inline float getPeak(int bin) const
    {
      if (bin < 0) bin = 0;
      if (bin > 127) bin = 127;
      return mpSpec->getFFTPeak(bin);
    }

    inline float getRms(int bin) const
    {
      if (bin < 0) bin = 0;
      if (bin > 127) bin = 127;
      return mpSpec->getFFTRms(bin);
    }

    static inline float dbNorm(float mag)
    {
      float db = 20.0f * log10f(mag + 1e-10f);
      return (db + 60.0f) / 60.0f;
    }

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);

      if (!mpSpec)
        return;

      float sr = 48000.0f;
      float logMin = log2f(20.0f);
      float logMax = log2f(sr * 0.5f);
      float logRange = logMax - logMin;
      float binHz = sr / 256.0f;

      int w = mWidth < kMaxWidth ? mWidth : kMaxWidth;
      float h = (float)mHeight;

      float peakH[kMaxWidth];
      float rmsH[kMaxWidth];
      float peakMagMax = 0.0f;

      for (int px = 0; px < w; px++)
      {
        float t = (float)px / (float)(w > 1 ? w - 1 : 1);
        float hz = powf(2.0f, logMin + t * logRange);
        float binFloat = hz / binHz;

        float nextT = (float)(px + 1) / (float)(w > 1 ? w - 1 : 1);
        float nextHz = powf(2.0f, logMin + nextT * logRange);
        float binsPerPixel = (nextHz - hz) / binHz;
        float tau = 0.7f - binsPerPixel * 0.08f;
        if (tau < 0.15f) tau = 0.15f;
        if (tau > 0.7f) tau = 0.7f;

        int b0 = (int)binFloat;
        float frac = binFloat - (float)b0;

        float peakVal = catmullRom(
            getPeak(b0 - 1), getPeak(b0), getPeak(b0 + 1), getPeak(b0 + 2),
            frac, tau);
        float rmsVal = catmullRom(
            getRms(b0 - 1), getRms(b0), getRms(b0 + 1), getRms(b0 + 2),
            frac, tau);

        if (peakVal < 0.0f) peakVal = 0.0f;
        if (rmsVal < 0.0f) rmsVal = 0.0f;

        float peakNorm = dbNorm(peakVal);
        float rmsNorm = dbNorm(rmsVal);
        if (peakNorm < 0.0f) peakNorm = 0.0f;
        if (peakNorm > 1.0f) peakNorm = 1.0f;
        if (rmsNorm < 0.0f) rmsNorm = 0.0f;
        if (rmsNorm > 1.0f) rmsNorm = 1.0f;

        peakH[px] = peakNorm * h;
        rmsH[px] = rmsNorm * h;

        if (peakVal > peakMagMax)
          peakMagMax = peakVal;
      }

      int bot = mWorldBottom;
      int prevPeakY = -1;

      for (int px = 0; px < w; px++)
      {
        int x = mWorldLeft + px;
        int rH = (int)rmsH[px];
        int pH = (int)peakH[px];
        if (rH > mHeight - 1) rH = mHeight - 1;
        if (pH > mHeight - 1) pH = mHeight - 1;

        if (rH > 0)
        {
          float colBright = (peakMagMax > 1e-10f)
                                ? (rmsH[px] / (peakH[px] > 0.1f ? peakH[px] : 0.1f))
                                : 0.5f;
          if (colBright > 1.0f) colBright = 1.0f;
          int maxGray = 4 + (int)(colBright * 7.0f);
          if (maxGray > 11) maxGray = 11;
          int minGray = 2;

          for (int y = 0; y < rH; y++)
          {
            float yt = (float)y / (float)(rH > 1 ? rH - 1 : 1);
            int gray = maxGray - (int)(yt * (float)(maxGray - minGray));
            if (gray < 2) gray = 2;
            fb.pixel(gray, x, bot + y);
          }
        }
        int peakY = bot + pH;
        if (prevPeakY >= 0)
          fb.line(WHITE, x - 1, prevPeakY, x, peakY);
        prevPeakY = peakY;
      }
    }
  };

} // namespace scope_unit
