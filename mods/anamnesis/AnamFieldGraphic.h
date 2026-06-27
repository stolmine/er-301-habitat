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
      const float mixN = mpOp ? mpOp->vizMix() : 1.0f;
      // Base streamline brightness scales with Mix; droplet glow adds on top.
      const float baseB = field::kBaseDim + (field::kBaseBright - field::kBaseDim) * mixN;

      // Cache the active rain droplets once (epicenters in content-x / column-y).
      int nd = 0;
      float dX[kVizMaxDrops], dY[kVizMaxDrops], dAge[kVizMaxDrops];
      float dC[kVizMaxDrops], dAmp[kVizMaxDrops];
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
          dAmp[nd] = mpOp->vizDropAmp(i);
          nd++;
        }
      }

      // Each flow line is sampled at control points every kCtrlStep px (the
      // expensive flow + rain evals), then Catmull-Rom interpolated to per-pixel
      // y -> smooth curves, cheap enough to scale the line count.
      const int cstep = field::kCtrlStep;
      // GLOBAL control grid: control points sit at multiples of cstep in CONTENT
      // x, shared by every ply. The 43px ply stride isn't a multiple of cstep, so
      // a ply-relative grid would misalign at seams and the spline would break
      // where a ripple bends it. Sharing the grid -> neighbours sample identical
      // control points at the boundary -> one continuous curve across the seam.
      const int g0 = x0 / cstep - 1;        // first grid index (one margin before)
      const int gLast = (x0 + w) / cstep + 2; // covers the 1px bridge column too
      int mctrl = gLast - g0 + 1;
      if (mctrl > 40) mctrl = 40;
      float ctrlY[40], ctrlB[40]; // control-point y (bent) and ring glow
      // Bridge the 1px SpottedStrip gap: every ply but the last draws one extra
      // content column (px = left+w) so the continuous curve crosses into the
      // next ply with no hairline seam. The last ply stops at its own edge.
      const int wext = (mIndex < mCount - 1) ? 1 : 0;
      for (int s = 0; s < n; s++)
      {
        const float yb = field::baseline(s, n, h);
        for (int i = 0; i < mctrl; i++)
        {
          const float cx = (float)((g0 + i) * cstep); // global grid content-x
          float y0 = yb + field::flow(cx, yb, phase);
          float glow = 0.0f;
          // Rain BENDS (geometry) and GLOWS (illumination) the line, each scaled
          // by its capture loudness. Shared pond -> drops cross ply seams.
          for (int d = 0; d < nd; d++)
          {
            const field::RippleHit hit = field::rippleEval(cx - dX[d], y0 - dY[d], dAge[d], dC[d]);
            y0 += dAmp[d] * hit.bend;
            glow += dAmp[d] * hit.glow;
          }
          ctrlY[i] = y0;
          ctrlB[i] = glow;
        }
        float prevY = -1000.0f;
        for (int lx = 0; lx < w + wext; lx++)
        {
          const int cx = x0 + lx;
          const int seg = cx / cstep;         // global grid segment
          const int idx = seg - g0;            // index of this segment's p1 in ctrl
          const float t = (float)(cx - seg * cstep) / (float)cstep;
          float y = field::catmull(ctrlY[idx - 1], ctrlY[idx], ctrlY[idx + 1], ctrlY[idx + 2], t);
          float glow = field::catmull(ctrlB[idx - 1], ctrlB[idx], ctrlB[idx + 1], ctrlB[idx + 2], t);
          if (y < 0.0f)
            y = 0.0f;
          else if (y > (float)(h - 1))
            y = (float)(h - 1);
          // Brightness: Mix-scaled base + droplet ring glow (glow not Mix-scaled,
          // so rings stay high-contrast against the lines at any wet level).
          float bf = baseB + glow * field::kGlowGain;
          if (bf < 0.0f) bf = 0.0f; else if (bf > 15.0f) bf = 15.0f;
          const int bri = (int)(bf + 0.5f);
          const int px = left + lx;
          // sqrt-coverage anti-alias: crisp bright core, smooth sub-pixel edge.
          const int yi = (int)y;
          const float f = y - (float)yi;
          fb.pixel((int)(bri * sqrtf(1.0f - f) + 0.5f), px, bot + yi);
          if (yi + 1 < h)
            fb.pixel((int)(bri * sqrtf(f) + 0.5f), px, bot + yi + 1);
          // Solid-fill steep gaps to the previous column so the line is unbroken.
          if (prevY > -999.0f)
          {
            const int a = (int)(prevY < y ? prevY : y);
            const int b = (int)(prevY < y ? y : prevY);
            for (int yy = a + 1; yy < b; yy++)
              fb.pixel(bri, px, bot + yy);
          }
          prevY = y;
        }
      }

      // Impact splashes are global now (rain falls across the whole strip), so
      // every ply draws the flecks for drops whose epicenter is in its window.
      drawLooper(fb, left, bot, w, x0);
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
