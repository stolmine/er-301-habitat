#pragma once

#include <od/graphics/Graphic.h>
#include "MultibandSaturator.h"
#include <math.h>
#include <stdio.h>

namespace stolmine
{

  // Max ply width for static arrays
  static const int kMaxSpectrumWidth = 64;

  class SpectrumGraphic : public od::Graphic
  {
  public:
    SpectrumGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height),
          mBandIndex(0), mpSat(0) {}

    virtual ~SpectrumGraphic()
    {
      if (mpSat)
        mpSat->release();
    }

    void follow(MultibandSaturator *p)
    {
      if (mpSat)
        mpSat->release();
      mpSat = p;
      if (mpSat)
        mpSat->attach();
    }

    void setBandIndex(int i) { mBandIndex = i; }

  private:
    int mBandIndex;
    MultibandSaturator *mpSat;

    // Catmull-Rom spline with tension parameter
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

    // Safe bin magnitude read with clamping
    inline float getPeak(int bin) const
    {
      if (bin < 0) bin = 0;
      if (bin > 127) bin = 127;
      return mpSat->getFFTPeak(bin);
    }

    inline float getRms(int bin) const
    {
      if (bin < 0) bin = 0;
      if (bin > 127) bin = 127;
      return mpSat->getFFTRms(bin);
    }

    // dB conversion: 60dB range mapped to 0-1
    static inline float dbNorm(float mag)
    {
      float db = 20.0f * log10f(mag + 1e-10f);
      return (db + 60.0f) / 60.0f; // -60dB=0, 0dB=1
    }

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      // Black background
      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);

      if (!mpSat)
        return;

      float level = mpSat->getBandLevel(mBandIndex);
      bool muted = mpSat->getBandMuted(mBandIndex);

      if (muted)
      {
        fb.fill(GRAY3, mWorldLeft, mWorldBottom,
                mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);
        return;
      }

      // Frequency range for this band
      float sr = 48000.0f;
      float minHz, maxHz;
      if (mBandIndex == 0)
      {
        minHz = 20.0f;
        maxHz = mpSat->getCrossoverFreq(0);
      }
      else if (mBandIndex == 1)
      {
        minHz = mpSat->getCrossoverFreq(0);
        maxHz = mpSat->getCrossoverFreq(1);
      }
      else
      {
        minHz = mpSat->getCrossoverFreq(1);
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
      int w = mWidth < kMaxSpectrumWidth ? mWidth : kMaxSpectrumWidth;
      float h = (float)mHeight;

      // --- Pass 1: interpolate peak and RMS heights per pixel ---
      float peakH[kMaxSpectrumWidth];
      float rmsH[kMaxSpectrumWidth];
      float peakMagMax = 0.0f;

      for (int px = 0; px < w; px++)
      {
        float t = (float)px / (float)(w > 1 ? w - 1 : 1);
        float hz = powf(2.0f, logMin + t * logRange);
        float binFloat = hz / binHz;

        // Adaptive tension: fewer bins per pixel = smoother
        float nextT = (float)(px + 1) / (float)(w > 1 ? w - 1 : 1);
        float nextHz = powf(2.0f, logMin + nextT * logRange);
        float binsPerPixel = (nextHz - hz) / binHz;
        float tau = binsPerPixel * 0.08f;
        tau = 0.7f - tau;
        if (tau < 0.15f) tau = 0.15f;
        if (tau > 0.7f) tau = 0.7f;

        // Catmull-Rom interpolation across 4 surrounding bins
        int b0 = (int)binFloat;
        float frac = binFloat - (float)b0;

        float peakVal = catmullRom(
            getPeak(b0 - 1), getPeak(b0), getPeak(b0 + 1), getPeak(b0 + 2),
            frac, tau);
        float rmsVal = catmullRom(
            getRms(b0 - 1), getRms(b0), getRms(b0 + 1), getRms(b0 + 2),
            frac, tau);

        // No per-band level scaling on spectrum height -- preserves crossover stitching
        if (peakVal < 0.0f) peakVal = 0.0f;
        if (rmsVal < 0.0f) rmsVal = 0.0f;

        // Convert to dB-scaled pixel height
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

      // --- Pass 2: draw ---
      int bot = mWorldBottom;
      int prevPeakY = -1;

      for (int px = 0; px < w; px++)
      {
        int x = mWorldLeft + px;
        int rH = (int)rmsH[px];
        int pH = (int)peakH[px];
        if (rH > mHeight - 1) rH = mHeight - 1;
        if (pH > mHeight - 1) pH = mHeight - 1;

        // Per-pixel RMS gradient fill
        if (rH > 0)
        {
          // Per-bin brightness: scale max gray by this column's RMS relative to peak
          float colBright = (peakMagMax > 1e-10f)
                                ? (rmsH[px] / (peakH[px] > 0.1f ? peakH[px] : 0.1f))
                                : 0.5f;
          if (colBright > 1.0f) colBright = 1.0f;
          // Scale brightness by band level (0=dark, 1=normal, 2=bright)
          float levelBright = level > 2.0f ? 2.0f : level;
          int maxGray = (int)((4.0f + colBright * 7.0f) * (levelBright * 0.5f));
          if (maxGray < 2) maxGray = 2;
          if (maxGray > 13) maxGray = 13;
          int minGray = maxGray > 4 ? maxGray - 6 : 2;

          for (int y = 0; y < rH; y++)
          {
            float yt = (float)y / (float)(rH > 1 ? rH - 1 : 1);
            int gray = maxGray - (int)(yt * (float)(maxGray - minGray));
            if (gray < 1) gray = 1;
            fb.pixel(gray, x, bot + y);
          }
        }

        // Peak contour (smooth line, brightness scaled by level)
        int peakColor = (int)(level * 7.5f);
        if (peakColor < 3) peakColor = 3;
        if (peakColor > WHITE) peakColor = WHITE;
        int peakY = bot + pH;
        if (prevPeakY >= 0)
          fb.line(peakColor, x - 1, prevPeakY, x, peakY);
        prevPeakY = peakY;
      }

      // dB readout in top-right corner
      float db = (level > 0.001f) ? 20.0f * log10f(level) : -60.0f;
      char buf[8];
      snprintf(buf, sizeof(buf), "%+.0f", db);
      fb.text(GRAY10, mWorldLeft + mWidth - 18,
              mWorldBottom + mHeight - 10, buf, 10);
    }
  };

} // namespace stolmine
