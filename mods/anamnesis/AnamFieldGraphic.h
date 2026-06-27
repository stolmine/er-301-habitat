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

      for (int s = 0; s < n; s++)
      {
        const float yb = field::baseline(s, n, h);
        int prevPy = -1000;
        for (int lx = 0; lx < w; lx++)
        {
          const float cx = (float)(x0 + lx);
          float yy = yb + field::flow(cx, yb, phase);
          if (yy < 0.0f)
            yy = 0.0f;
          else if (yy > (float)(h - 1))
            yy = (float)(h - 1);
          const int px = left + lx;
          const int py = bot + (int)(yy + 0.5f);
          // Connect to the previous column so each streamline is unbroken.
          if (prevPy > -1000 && prevPy != py)
          {
            const int a = prevPy < py ? prevPy : py;
            const int b = prevPy < py ? py : prevPy;
            fb.vline(WHITE, px, a, b);
          }
          else
          {
            fb.pixel(WHITE, px, py);
          }
          prevPy = py;
        }
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
