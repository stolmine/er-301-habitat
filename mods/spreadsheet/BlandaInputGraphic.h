// BlandaInputGraphic -- per-input-ply graphic overlay.
//
// Three instances, one per input ply. Each reads the SAME Blanda op and
// renders only its 1/3 slice of the global scan axis. Lined up edge to
// edge they form a single continuous bell landscape.
//
// Layers (bottom up):
//   1. Bell landscape (this slice) drawn as polylines (connected segments)
//   2. Short vertical WHITE tick at this input's current Offset
//   3. Rising level indicator at the scan column: vertical line from y=0
//      up to the bell value at scan. Per-pixel inverts shade against
//      whatever backdrop (MiniScope waveform) sits beneath.
//   4. WHITE dashed vertical scan-position playhead (only when scan is
//      in this ply's slice)
//
// Axis-aligned only; no runtime sinf/cosf (see memory feedback_package_trig_lut).
// fb.readPixel() is used inside the rising-level inversion loop. The vtable
// position of readPixel was sorted out firmware-side in 00f2992 and has
// been stable since.

#pragma once

#include <od/graphics/Graphic.h>
#include <math.h>
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
    // Evaluate the shaped bell at scan position, picking the max across
    // all three inputs. Shared by landscape + rising indicator.
    float bellValueAt(float globalScan)
    {
      float top = 0.0f;
      for (int b = 0; b < kBlandaInputs; b++)
      {
        float off = mpBlanda->getInputOffset(b);
        float wt  = mpBlanda->getInputWeight(b);
        float sh  = mpBlanda->getInputShape(b);
        float ww  = wt * mpBlanda->getFocusWidth();
        if (ww < 1.0f / 512.0f) ww = 1.0f / 512.0f;
        float d = globalScan - off;
        if (d < 0.0f) d = -d;
        float x = d / ww;
        if (x >= 1.0f) continue;
        float gamma = 1.0f + sh * 3.0f;
        float c = 1.0f - powf(x, gamma);
        if (c > top) top = c;
      }
      return top;
    }

    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpBlanda) return;

      const int x0 = mWorldLeft;
      const int y0 = mWorldBottom;
      const int w  = mWidth;
      const int h  = mHeight;

      const float sliceSize = 1.0f / (float)kBlandaInputs;
      const float slice0 = (float)mIndex * sliceSize;
      const float slice1 = slice0 + sliceSize;

      const float scanPos = mpBlanda->getScanPos();

      // 1. Bell landscape as polyline. Connect consecutive pixel columns
      // with fb.line for a continuous silhouette.
      int prevX = -1, prevY = 0;
      for (int px = 0; px < w; px++)
      {
        float globalScan = slice0 + ((float)px / (float)w) * sliceSize;
        float top = bellValueAt(globalScan);

        if (top > 0.0f)
        {
          int bellY = y0 + (int)(top * (float)(h - 2));
          if (bellY >= y0 + h) bellY = y0 + h - 1;
          int wx = x0 + px;

          if (prevX >= 0)
            fb.line(WHITE, prevX, prevY, wx, bellY);
          else
            fb.pixel(WHITE, wx, bellY);

          prevX = wx;
          prevY = bellY;
        }
        else
        {
          prevX = -1; // break the polyline across zero-bell gaps
        }
      }

      // 2. Peak tick for this input.
      {
        float myOffset = mpBlanda->getInputOffset(mIndex);
        if (myOffset >= slice0 && myOffset < slice1)
        {
          int tx = x0 + (int)((myOffset - slice0) / sliceSize * (float)w);
          if (tx < x0) tx = x0;
          if (tx >= x0 + w) tx = x0 + w - 1;
          int peakY = y0 + (h - 2);
          int baseY = peakY - 5;
          if (baseY < y0) baseY = y0;
          fb.line(WHITE, tx, baseY, tx, peakY);
        }
      }

      // 3. Rising level indicator at scan column. Vertical line from the
      // bottom up to the bell's value at scan; per-pixel reads the
      // backdrop and picks a contrasting shade so it stays visible when
      // it crosses the MiniScope waveform.
      if (scanPos >= slice0 && scanPos < slice1)
      {
        int sx = x0 + (int)((scanPos - slice0) / sliceSize * (float)w);
        if (sx < x0) sx = x0;
        if (sx >= x0 + w) sx = x0 + w - 1;

        float top = bellValueAt(scanPos);
        int topY = y0 + (int)(top * (float)(h - 2));
        if (topY >= y0 + h) topY = y0 + h - 1;

        // Draw base-to-top vertical; each pixel inverts against whatever
        // is already there.
        for (int py = y0; py <= topY; py++)
        {
          int under = fb.readPixel(sx, py);
          int shade = (under > 7) ? GRAY3 : WHITE;
          fb.pixel(shade, sx, py);
        }

        // 4. Scan playhead carrying past the top of the level indicator
        // to the top of the ply (dashed 1-on / 2-off), so the full axis
        // is legible. Only the section above the rising indicator uses
        // dashes; below it is the solid inverted line.
        for (int py = topY + 1; py < y0 + h; py += 3)
          fb.pixel(WHITE, sx, py);
      }
    }
#endif

  private:
    Blanda *mpBlanda = 0;
    int mIndex = 0;
  };

} // namespace stolmine
