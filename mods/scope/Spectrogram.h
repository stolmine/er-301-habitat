#pragma once

#include <od/objects/Object.h>
#include <od/graphics/Graphic.h>
#include <od/config.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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
    // FFT size (256 or 512): the wider ply units use 512 so the extra display width is
    // backed by twice the bins, not interpolation. Set once by the unit (not CV).
    od::Parameter mFFTSize{"FFT Size", 256.0f};
#endif

    // SWIG-visible
    float getFFTPeak(int bin);
    float getFFTRms(int bin);
    int getFFTSize();   // 256 or 512, current
    float getPeakHz();  // frequency of the greatest-energy bin (parabolic-interpolated)
    float getPeakDb();  // its level in dB

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

    // S1: frequency axis. 0 = log (default, EQ-style), 1 = linear.
    void setFreqMode(int m) { mFreqMode = (m == 1) ? 1 : 0; }
    // S2: vertical/amplitude mapping. 0 = log/dB (default), 1 = linear, 2 = expanded.
    void setAmpMode(int m) { mAmpMode = (m < 0) ? 0 : (m > 2 ? 2 : m); }

  private:
    Spectrogram *mpSpec;
    int mFreqMode = 0;
    int mAmpMode = 0;

    // Wide enough for the 6-ply variant (6 * SECTION_PLY(42) = 252 px). The peakH/rmsH
    // scratch arrays below are draw-thread stack (not audio), so 256 floats each is fine.
    static const int kMaxWidth = 256;

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
      int maxBin = mpSpec->getFFTSize() / 2 - 1;   // 127 (256) or 255 (512)
      if (bin < 0) bin = 0;
      if (bin > maxBin) bin = maxBin;
      return mpSpec->getFFTPeak(bin);
    }

    inline float getRms(int bin) const
    {
      int maxBin = mpSpec->getFFTSize() / 2 - 1;
      if (bin < 0) bin = 0;
      if (bin > maxBin) bin = maxBin;
      return mpSpec->getFFTRms(bin);
    }

    static inline float dbNorm(float mag)
    {
      float db = 20.0f * log10f(mag + 1e-10f);
      return (db + 60.0f) / 60.0f;
    }

    // Amplitude -> normalized height, per S2 mode. All clamp to [0,1] at the call site.
    inline float ampNorm(float mag) const
    {
      if (mAmpMode == 1)            // linear magnitude (peaks dominate)
        return mag * 4.0f;
      if (mAmpMode == 2)           // expanded: sqrt lifts quiet detail
      {
        float v = mag * 4.0f;
        if (v < 0.0f) v = 0.0f;
        return sqrtf(v);
      }
      return dbNorm(mag);          // log / dB (default)
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
      float binHz = sr / (float)mpSpec->getFFTSize();   // 256 or 512-pt

      int w = mWidth < kMaxWidth ? mWidth : kMaxWidth;
      float h = (float)mHeight;

      float peakH[kMaxWidth];
      float rmsH[kMaxWidth];
      float peakMagMax = 0.0f;

      float nyq = sr * 0.5f;
      for (int px = 0; px < w; px++)
      {
        float t = (float)px / (float)(w > 1 ? w - 1 : 1);
        float nextT = (float)(px + 1) / (float)(w > 1 ? w - 1 : 1);
        // S1: log (equal octaves/pixel) or linear (equal Hz/pixel) frequency axis.
        float hz, nextHz;
        if (mFreqMode == 1)
        {
          hz = 20.0f + t * (nyq - 20.0f);
          nextHz = 20.0f + nextT * (nyq - 20.0f);
        }
        else
        {
          hz = powf(2.0f, logMin + t * logRange);
          nextHz = powf(2.0f, logMin + nextT * logRange);
        }
        float binFloat = hz / binHz;

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

        float peakNorm = ampNorm(peakVal);
        float rmsNorm = ampNorm(rmsVal);
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

  // S3 read-only readout: the greatest-energy frequency + its level, drawn in the
  // sub-display (apes ScopeVoltsReadout's styling). Right-justified so columns stay put.
  class SpectrogramReadout : public od::Graphic
  {
  public:
    SpectrogramReadout(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpSpec(0) {}

    virtual ~SpectrogramReadout()
    {
      if (mpSpec) mpSpec->release();
    }

    void follow(Spectrogram *p)
    {
      if (mpSpec) mpSpec->release();
      mpSpec = p;
      if (mpSpec) mpSpec->attach();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      od::Graphic::draw(fb);
      if (!mpSpec)
        return;

      float hz = mpSpec->getPeakHz();
      float db = mpSpec->getPeakDb();

      char buf[24];
      if (hz >= 1000.0f)
        snprintf(buf, sizeof(buf), "%.2fk", hz / 1000.0f);
      else
        snprintf(buf, sizeof(buf), "%.0f", hz);

      char dbuf[16];
      snprintf(dbuf, sizeof(dbuf), "%.0fdB", db);

      // Frequency on the upper line, level below - both size 10 (the dB was hard to read at
      // size 8), right-justified so the columns stay put as digits change.
      int fw = (int)strlen(buf) * 6;
      fb.text(WHITE, mWorldLeft + mWidth - fw - 2, mWorldBottom + mHeight - 10, buf, 10);
      int dw = (int)strlen(dbuf) * 6;
      fb.text(11, mWorldLeft + mWidth - dw - 2, mWorldBottom, dbuf, 10);   // level, slightly dimmer
    }

  private:
    Spectrogram *mpSpec;
#endif
  };

} // namespace scope_unit
