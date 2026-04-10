#pragma once

#include <od/graphics/Graphic.h>
#include "MultibandCompressor.h"
#include <math.h>
#include <stdio.h>

namespace stolmine
{

  static const int kMaxCompSpecWidth = 64;

  class CompressorSpectrumGraphic : public od::Graphic
  {
  public:
    CompressorSpectrumGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height),
          mBandIndex(0), mpComp(0) {}

    virtual ~CompressorSpectrumGraphic()
    {
      if (mpComp)
        mpComp->release();
    }

    void follow(MultibandCompressor *p)
    {
      if (mpComp)
        mpComp->release();
      mpComp = p;
      if (mpComp)
        mpComp->attach();
    }

    void setBandIndex(int i) { mBandIndex = i; }

  private:
    int mBandIndex;
    MultibandCompressor *mpComp;
    float mGrSlew[kMaxCompSpecWidth]; // per-pixel GR ceiling (slewed)
    bool mSlewInit = false;

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

    inline float getRms(int bin) const
    {
      if (bin < 0) bin = 0;
      if (bin > 127) bin = 127;
      return mpComp->getFFTRms(bin);
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

      if (!mpComp)
        return;

      if (!mSlewInit)
      {
        for (int i = 0; i < kMaxCompSpecWidth; i++)
          mGrSlew[i] = 1.0f;
        mSlewInit = true;
      }

      // Frequency range for this band
      float sr = 48000.0f;
      float minHz, maxHz;
      if (mBandIndex == 0)
      {
        minHz = 20.0f;
        maxHz = mpComp->getCrossoverFreq(0);
      }
      else if (mBandIndex == 1)
      {
        minHz = mpComp->getCrossoverFreq(0);
        maxHz = mpComp->getCrossoverFreq(1);
      }
      else
      {
        minHz = mpComp->getCrossoverFreq(1);
        maxHz = sr * 0.5f;
      }
      if (maxHz <= minHz)
        maxHz = minHz + 1.0f;

      float logMin = log2f(minHz > 1.0f ? minHz : 1.0f);
      float logMax = log2f(maxHz > 2.0f ? maxHz : 2.0f);
      float logRange = logMax - logMin;
      if (logRange < 0.01f)
        logRange = 0.01f;

      float binHz = sr / 256.0f;
      int w = mWidth < kMaxCompSpecWidth ? mWidth : kMaxCompSpecWidth;
      float h = (float)mHeight;

      // Current band GR (0 = no reduction, 1 = full reduction)
      float bandGR = mpComp->getBandGainReduction(mBandIndex);

      // --- Pass 1: interpolate RMS heights and GR ceiling per pixel ---
      float rmsH[kMaxCompSpecWidth];

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

        float rmsVal = catmullRom(
            getRms(b0 - 1), getRms(b0), getRms(b0 + 1), getRms(b0 + 2),
            frac, tau);
        if (rmsVal < 0.0f) rmsVal = 0.0f;

        float rmsNorm = dbNorm(rmsVal);
        if (rmsNorm < 0.0f) rmsNorm = 0.0f;
        if (rmsNorm > 1.0f) rmsNorm = 1.0f;

        rmsH[px] = rmsNorm * h;

        // GR ceiling: fast attack (1 frame), slower release
        float grTarget = 1.0f - bandGR; // 1 = full height (no reduction), 0 = fully squashed
        float grCoeff = grTarget < mGrSlew[px] ? 0.6f : 0.08f; // fast down, slow up
        mGrSlew[px] += (grTarget - mGrSlew[px]) * grCoeff;
      }

      // --- Pass 2: draw ---
      int bot = mWorldBottom;
      int prevGrY = -1;

      for (int px = 0; px < w; px++)
      {
        int x = mWorldLeft + px;
        int rH = (int)rmsH[px];
        if (rH > mHeight - 1) rH = mHeight - 1;

        // GR ceiling height
        int grH = (int)(mGrSlew[px] * h);
        if (grH > mHeight - 1) grH = mHeight - 1;

        // RMS gradient fill (capped at GR ceiling)
        int drawH = rH < grH ? rH : grH;
        if (drawH > 0)
        {
          int maxGray = 4 + (int)((float)drawH / (h > 1 ? h : 1) * 7.0f);
          if (maxGray > 11) maxGray = 11;
          int minGray = 2;

          for (int y = 0; y < drawH; y++)
          {
            float yt = (float)y / (float)(drawH > 1 ? drawH - 1 : 1);
            int gray = maxGray - (int)(yt * (float)(maxGray - minGray));
            if (gray < 2) gray = 2;
            fb.pixel(gray, x, bot + y);
          }
        }

        // GR ceiling line (connected segments, like Parfait's peak line)
        int grY = bot + grH;
        if (prevGrY >= 0)
          fb.line(10, x - 1, prevGrY, x, grY); // gray 10 = bright but not WHITE
        prevGrY = grY;
      }

      // GR readout in top-right corner
      float grDb = bandGR > 0.001f ? 20.0f * log10f(1.0f - bandGR + 1e-10f) : 0.0f;
      char buf[8];
      snprintf(buf, sizeof(buf), "%.0f", grDb);
      fb.text(WHITE, mWorldLeft + mWidth - 20, mWorldBottom + mHeight - 10, buf, 10);
    }
  };

} // namespace stolmine
