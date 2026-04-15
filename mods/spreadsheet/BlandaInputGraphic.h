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

      // 1. Bell landscape: each bell is drawn as its OWN continuous
      // polyline with the shade chosen per column by its role --
      //   WHITE  when the bell IS the silhouette (coef == maxCoef)
      //   GRAY5  when the bell is hidden AND strictly heavier than every
      //          other active bell at this column ("dominant ghost")
      //   skip   when the bell is hidden and not strictly dominant
      //
      // Drawing each bell as one polyline (with shade varying between
      // segments) keeps the curve connected through the silhouette-to-
      // ghost handoff, so the strongest bell's ghost runs right out of
      // its own peak without a pixel-jump aura.
      float weight[kBlandaInputs];
      for (int b = 0; b < kBlandaInputs; b++)
        weight[b] = mpBlanda->getInputWeight(b);

      int prevBellX[kBlandaInputs] = {-1, -1, -1};
      int prevBellY[kBlandaInputs] = {0, 0, 0};

      for (int px = 0; px < w; px++)
      {
        float globalScan = slice0 + ((float)px / (float)w) * sliceSize;
        int wx = x0 + px;

        // Evaluate every bell at this column.
        float coef[kBlandaInputs];
        float maxCoef = 0.0f;
        for (int b = 0; b < kBlandaInputs; b++)
        {
          float off = mpBlanda->getInputOffset(b);
          float ww  = weight[b] * mpBlanda->getFocusWidth();
          if (ww < 1.0f / 512.0f) ww = 1.0f / 512.0f;
          float d = globalScan - off;
          if (d < 0.0f) d = -d;
          float xn = d / ww;
          if (xn >= 1.0f) { coef[b] = 0.0f; continue; }
          float gamma = 1.0f + mpBlanda->getInputShape(b) * 3.0f;
          float c = 1.0f - powf(xn, gamma);
          coef[b] = (c > 0.0f) ? c : 0.0f;
          if (coef[b] > maxCoef) maxCoef = coef[b];
        }

        // Per-bell polyline with shade-per-segment.
        for (int b = 0; b < kBlandaInputs; b++)
        {
          if (coef[b] <= 0.0f)
          {
            prevBellX[b] = -1;
            continue;
          }

          int shade;
          if (coef[b] >= maxCoef)
          {
            // This bell IS the silhouette at this column.
            shade = WHITE;
          }
          else
          {
            // Hidden. Ghost only if strictly heavier than every other
            // active bell.
            bool strictlyDominant = true;
            for (int j = 0; j < kBlandaInputs; j++)
            {
              if (j == b) continue;
              if (coef[j] > 0.0f && weight[j] >= weight[b])
              {
                strictlyDominant = false;
                break;
              }
            }
            if (!strictlyDominant)
            {
              // Break this bell's polyline; it is hidden and not dominant.
              prevBellX[b] = -1;
              continue;
            }
            shade = GRAY5;
          }

          int bellY = y0 + (int)(coef[b] * (float)(h - 2));
          if (bellY >= y0 + h) bellY = y0 + h - 1;

          if (prevBellX[b] >= 0)
            fb.line(shade, prevBellX[b], prevBellY[b], wx, bellY);
          else
            fb.pixel(shade, wx, bellY);
          prevBellX[b] = wx;
          prevBellY[b] = bellY;
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

      // 3. Horizontal dotted mix-coef indicator for this input. y tracks
      // the bell value at the current scan position, so as the user
      // sweeps scan the line rises and falls and meets the scan playhead
      // right on the bell curve. Per-pixel read the backdrop so the dots
      // invert shade (WHITE over dark, GRAY3 over bright) and stay
      // legible when crossing the MiniScope waveform.
      {
        float myCoef = mpBlanda->getMixCoef(mIndex);
        float myLevel = mpBlanda->getInputLevel(mIndex);
        int coefY = y0 + (int)(myCoef * (float)(h - 2));
        if (coefY >= y0 + h) coefY = y0 + h - 1;
        int active = (myLevel >= 0.001f);
        for (int px = 0; px < w; px += 3)
        {
          int wx = x0 + px;
          // Any non-black backdrop pixel flips the dot to its contrasting
          // shade -- MiniScope fills the waveform body in GRAY3 with WHITE
          // peak endpoints, both of which count as "drawn" here.
          int under = fb.readPixel(wx, coefY);
          int shade;
          if (active)
            shade = (under > 0) ? BLACK : WHITE;
          else
            shade = (under > 0) ? BLACK : GRAY4;
          fb.pixel(shade, wx, coefY);
        }
      }

      // 4. Scan-position playhead. WHITE dashed vertical (1 px on / 2
      // px off) drawn only by the ply whose slice contains scan, so
      // exactly one ply shows it at any time.
      if (scanPos >= slice0 && scanPos < slice1)
      {
        int sx = x0 + (int)((scanPos - slice0) / sliceSize * (float)w);
        if (sx < x0) sx = x0;
        if (sx >= x0 + w) sx = x0 + w - 1;
        for (int py = y0; py < y0 + h; py += 3)
          fb.pixel(WHITE, sx, py);
      }
    }
#endif

  private:
    Blanda *mpBlanda = 0;
    int mIndex = 0;
  };

} // namespace stolmine
