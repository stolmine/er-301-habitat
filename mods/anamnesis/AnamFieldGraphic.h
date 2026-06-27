// anamnesis::AnamFieldGraphic -- one ply's window into the all-over flow-field.
//
// Each ply's main graphic (replacing the stock fader) renders content-X
// [index*kStride .. +width] of the shared "Pond of Recollection" field, so the
// whole SpottedStrip reads as one continuous flowing image that pans as you
// scrub. Holds an Anamnesis* and reads its viz getters directly (Helicase
// pattern). planning/spatial-glitch-impl/07-allover-viz.md
//
// Phase 5b foundation: the baseline braided current only. Per-ply features
// (ripples / vortex / crystal / moire / fade) keyed off `mFeature` land in Phase C.

#pragma once

#include <od/graphics/Graphic.h>
#include "atoms/Anamnesis.h"
#include "AnamField.h"

namespace anamnesis
{

  class AnamFieldGraphic : public od::Graphic
  {
  public:
    AnamFieldGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~AnamFieldGraphic()
    {
      if (mpOp)
        mpOp->release();
    }

    void follow(Anamnesis *op)
    {
      if (mpOp)
        mpOp->release();
      mpOp = op;
      if (mpOp)
        mpOp->attach();
    }

    // Slice `index` of `count` plies; `feature` selects the per-ply motif (Phase C).
    void setCanvas(int index, int count)
    {
      mIndex = index;
      mCount = count;
    }
    void setFeature(int feature) { mFeature = feature; }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      const int w = mWidth;
      const int h = mHeight;
      const int left = mWorldLeft;
      const int bot = mWorldBottom;
      const int x0 = mIndex * field::kStride; // content-x of this slice's left edge

      const float phase = mpOp ? mpOp->vizPhase() : 0.0f;
      const int n = field::kStreamlines;

      // Cache the active rain droplets once (epicenters in content-x / column-y),
      // so the per-point bend loop touches locals, not the op pointer.
      int nd = 0;
      float dX[kVizMaxDrops], dY[kVizMaxDrops], dAge[kVizMaxDrops];
      float dC[kVizMaxDrops], dPh[kVizMaxDrops], dAmp[kVizMaxDrops];
      if (mpOp)
      {
        for (int i = 0; i < kVizMaxDrops; i++)
        {
          const float age = mpOp->vizDropAge(i);
          if (age < 0.0f)
            continue;
          dX[nd] = mpOp->vizDropX(i);
          dY[nd] = mpOp->vizDropY(i);
          dAge[nd] = age;
          dC[nd] = mpOp->vizDropSpeed(i);
          dPh[nd] = mpOp->vizDropPhase(i);
          dAmp[nd] = mpOp->vizDropAmp(i);
          nd++;
        }
      }

      // Each flow line is sampled at control points every kCtrlStep px (the
      // expensive flow + rain evals), then Catmull-Rom interpolated to per-pixel
      // y -> smooth curves, cheap enough to scale the line count.
      const int cstep = field::kCtrlStep;
      const int mctrl = (w - 1) / cstep + 4; // incl one margin each side
      float ctrl[40];
      for (int s = 0; s < n; s++)
      {
        const float yb = field::baseline(s, n, h);
        for (int i = 0; i < mctrl; i++)
        {
          const float cx = (float)(x0 + (i - 1) * cstep);
          float y0 = yb + field::flow(cx, yb, phase);
          // Rain bends the line: each droplet's wavefront Y push, scaled by its
          // capture loudness. Shared pond -> drops bend across ply seams.
          for (int d = 0; d < nd; d++)
            y0 += dAmp[d] * field::rippleDispY(cx - dX[d], y0 - dY[d], dAge[d], dC[d], dPh[d]);
          ctrl[i] = y0;
        }
        float prevY = -1000.0f;
        for (int lx = 0; lx < w; lx++)
        {
          const int seg = lx / cstep;
          const float t = (float)(lx - seg * cstep) / (float)cstep;
          float y = field::catmull(ctrl[seg], ctrl[seg + 1], ctrl[seg + 2], ctrl[seg + 3], t);
          if (y < 0.0f)
            y = 0.0f;
          else if (y > (float)(h - 1))
            y = (float)(h - 1);
          const int px = left + lx;
          // Anti-aliased: split brightness across the two straddling pixels so the
          // curve reads sub-pixel-smooth (the display is grayscale 0..15).
          const int yi = (int)y;
          const float f = y - (float)yi;
          // sqrt coverage: keeps a crisp, bright core at the sub-pixel crossover
          // (linear 50/50 split reads soft) while staying smooth between pixels.
          fb.pixel((int)(WHITE * sqrtf(1.0f - f) + 0.5f), px, bot + yi);
          if (yi + 1 < h)
            fb.pixel((int)(WHITE * sqrtf(f) + 0.5f), px, bot + yi + 1);
          // Solid-fill steep gaps to the previous column so the line is unbroken.
          if (prevY > -999.0f)
          {
            const int a = (int)(prevY < y ? prevY : y);
            const int b = (int)(prevY < y ? y : prevY);
            for (int yy = a + 1; yy < b; yy++)
              fb.pixel(WHITE, px, bot + yy);
          }
          prevY = y;
        }
      }

      // Per-ply feature motif, composed on top of the shared current.
      switch (mFeature)
      {
      case field::feature::kLooper:
        drawLooper(fb, left, bot, w, x0);
        break;
      default:
        break;
      }
    }

    // ---- feature renderers ----

    // Looper: the impact splash. A bright fleck at each young droplet whose
    // epicenter falls in this ply's content window (the ring's line-bending is
    // already applied pond-wide above). Fades over the drop's first ~0.35 s.
    void drawLooper(od::FrameBuffer &fb, int left, int bot, int w, int x0)
    {
      if (!mpOp)
        return;
      for (int i = 0; i < kVizMaxDrops; i++)
      {
        const float age = mpOp->vizDropAge(i);
        if (age < 0.0f || age > 0.35f)
          continue;
        const float ex = mpOp->vizDropX(i);
        if (ex < (float)x0 || ex >= (float)(x0 + w))
          continue;
        const int px = left + (int)(ex - (float)x0);
        const int py = bot + (int)(mpOp->vizDropY(i) + 0.5f);
        float bright = 1.0f - age / 0.35f;
        int c = (int)(GRAY7 + (WHITE - GRAY7) * bright);
        if (c < GRAY7)
          c = GRAY7;
        else if (c > WHITE)
          c = WHITE;
        fb.fillCircle(c, px, py, 1);
      }
    }
#endif

  private:
    Anamnesis *mpOp = 0;
    int mIndex = 0;
    int mCount = 1;
    int mFeature = 0;
  };

} // namespace anamnesis
