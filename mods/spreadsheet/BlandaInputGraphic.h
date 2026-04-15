// BlandaInputGraphic -- per-input-ply graphic overlay.
//
// Three instances, one per input ply. Each reads the SAME Blanda op and
// renders only its 1/3 slice of the global scan axis. Lined up edge to
// edge they form a single continuous bell landscape.
//
// Draws (bottom up):
//   - triangular bell landscape (all 3 inputs' bells, clipped to this slice)
//   - short vertical WHITE tick at this input's current Offset position
//   - Impasto-style dotted horizontal for this input's live mix coefficient
//   - WHITE dashed vertical scan-position playhead (only when scan is in slice)
//
// Axis-aligned draws + integer pixel ops only; no runtime sinf/cosf.
// (See memory feedback_package_trig_lut: trig from package .so graphics
// miscomputes on am335x. We stay clear of it here.)

#pragma once

#include <od/graphics/Graphic.h>
#include <Blanda.h>

namespace stolmine
{
  class BlandaInputGraphic : public od::Graphic
  {
  public:
    BlandaInputGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~BlandaInputGraphic()
    {
      if (mpBlanda) mpBlanda->release();
    }

    void follow(Blanda *p)
    {
      if (mpBlanda) mpBlanda->release();
      mpBlanda = p;
      if (mpBlanda) mpBlanda->attach();
    }

    void setIndex(int idx)
    {
      if (idx < 0) idx = 0;
      if (idx >= kBlandaInputs) idx = kBlandaInputs - 1;
      mIndex = idx;
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpBlanda) return;

      const int x0 = mWorldLeft;
      const int y0 = mWorldBottom;
      const int w  = mWidth;
      const int h  = mHeight;

      // Slice: this ply covers global scan [slice0, slice1].
      const float sliceSize = 1.0f / (float)kBlandaInputs;
      const float slice0 = (float)mIndex * sliceSize;
      const float slice1 = slice0 + sliceSize;

      float scanPos = mpBlanda->getScanPos();
      float focusWidth = mpBlanda->getFocusWidth();

      // 1. Bell landscape: iterate x pixels, find the max bell height
      // across all three inputs at this scan value. Plot a single pixel
      // at the top of the landscape. Short vertical fills below would
      // make it a filled shape, but a line-only reads more legibly
      // against the MiniScope backdrop.
      for (int px = 0; px < w; px++)
      {
        float globalScan = slice0 + ((float)px / (float)w) * sliceSize;

        float topCoef = 0.0f;
        for (int b = 0; b < kBlandaInputs; b++)
        {
          float off = mpBlanda->getInputOffset(b);
          float wt  = mpBlanda->getInputWeight(b);
          float ww  = wt * focusWidth;
          if (ww < 1.0f / 512.0f) ww = 1.0f / 512.0f;
          float d = globalScan - off;
          if (d < 0.0f) d = -d;
          float c = 1.0f - d / ww;
          if (c > topCoef) topCoef = c;
        }

        if (topCoef > 0.0f)
        {
          int bellY = y0 + (int)(topCoef * (float)(h - 2));
          if (bellY >= y0 + h) bellY = y0 + h - 1;
          fb.pixel(WHITE, x0 + px, bellY);
        }
      }

      // 2. Peak tick for this input (only if its offset falls in our slice).
      {
        float myOffset = mpBlanda->getInputOffset(mIndex);
        if (myOffset >= slice0 && myOffset < slice1)
        {
          int tx = x0 + (int)((myOffset - slice0) / sliceSize * (float)w);
          if (tx < x0) tx = x0;
          if (tx >= x0 + w) tx = x0 + w - 1;
          // Find the bell peak height at this exact scan position for
          // the tick's upper extent.
          float wt = mpBlanda->getInputWeight(mIndex);
          float ww = wt * focusWidth;
          if (ww < 1.0f / 512.0f) ww = 1.0f / 512.0f;
          // At the peak, coef = 1.0 (assuming no clamping). Scale to
          // pixel-space.
          int peakY = y0 + (int)(1.0f * (float)(h - 2));
          int baseY = peakY - 5;
          if (baseY < y0) baseY = y0;
          if (peakY >= y0 + h) peakY = y0 + h - 1;
          fb.line(WHITE, tx, baseY, tx, peakY);
        }
      }

      // 3. Dotted horizontal mix-coef indicator (Impasto style: fb.pixel
      // every 3 px).
      {
        float myCoef = mpBlanda->getMixCoef(mIndex);
        float myLevel = mpBlanda->getInputLevel(mIndex);
        int coefY = y0 + (int)(myCoef * (float)(h - 2));
        if (coefY >= y0 + h) coefY = y0 + h - 1;
        int shade = (myLevel < 0.001f) ? GRAY4 : GRAY9;
        for (int px = 0; px < w; px += 3)
          fb.pixel(shade, x0 + px, coefY);
      }

      // 4. Scan-position playhead -- only this ply draws it if scan is
      // in our slice. Dashed: pixel every 3 y-steps.
      if (scanPos >= slice0 && scanPos < slice1)
      {
        int sx = x0 + (int)((scanPos - slice0) / sliceSize * (float)w);
        if (sx < x0) sx = x0;
        if (sx >= x0 + w) sx = x0 + w - 1;
        for (int py = 0; py < h; py += 3)
          fb.pixel(WHITE, sx, y0 + py);
      }
    }
#endif

  private:
    Blanda *mpBlanda = 0;
    int mIndex = 0;
  };

} // namespace stolmine
