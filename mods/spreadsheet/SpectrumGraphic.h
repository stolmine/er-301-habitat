#pragma once

#include <od/graphics/Graphic.h>
#include "MultibandSaturator.h"
#include <math.h>
#include <stdio.h>

namespace stolmine
{

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

    virtual void draw(od::FrameBuffer &fb)
    {
      // Opaque black background (covers fader underneath)
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

      // Determine frequency range for this band
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

      // Brightness from band level
      int peakColor = (level > 1.5f) ? WHITE : (level > 0.8f) ? GRAY13 : GRAY10;
      int rmsBot = (level > 1.5f) ? GRAY13 : (level > 0.8f) ? GRAY10 : GRAY7;
      int rmsTop = (level > 1.5f) ? GRAY7 : (level > 0.8f) ? GRAY5 : GRAY3;

      int prevPeakY = -1;
      for (int px = 0; px < mWidth; px++)
      {
        float t = (float)px / (float)(mWidth > 1 ? mWidth - 1 : 1);
        float hz = powf(2.0f, logMin + t * logRange);
        int bin = (int)(hz / binHz);
        if (bin < 0) bin = 0;
        if (bin > 127) bin = 127;

        float peak = mpSat->getFFTPeak(bin) * level;
        float rms = mpSat->getFFTRms(bin) * level;

        // Log dB scaling: 60dB range mapped to full height
        float peakDb = 20.0f * log10f(peak + 1e-10f);
        float rmsDb = 20.0f * log10f(rms + 1e-10f);
        float dbMin = -60.0f, dbMax = 0.0f;
        int peakH = (int)(((peakDb - dbMin) / (dbMax - dbMin)) * (float)mHeight);
        int rmsH = (int)(((rmsDb - dbMin) / (dbMax - dbMin)) * (float)mHeight);
        if (peakH > mHeight - 1) peakH = mHeight - 1;
        if (rmsH > mHeight - 1) rmsH = mHeight - 1;

        int x = mWorldLeft + px;
        int bot = mWorldBottom;

        // RMS fill (gradient)
        if (rmsH > 0)
        {
          int midH = rmsH / 2;
          if (midH > 0)
            fb.vline(rmsBot, x, bot, bot + midH);
          if (rmsH > midH)
            fb.vline(rmsTop, x, bot + midH, bot + rmsH);
        }

        // Peak contour
        int peakY = bot + peakH;
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

  private:
    int mBandIndex;
    MultibandSaturator *mpSat;
  };

} // namespace stolmine
