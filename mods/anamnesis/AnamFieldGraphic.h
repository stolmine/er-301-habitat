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
      const float mixN = mpOp ? mpOp->vizMix() : 1.0f;
      // Base streamline brightness scales with Mix; droplet glow adds on top.
      const float baseB = field::kBaseDim + (field::kBaseBright - field::kBaseDim) * mixN;

      // Density drives the WEAVE (merge/split), not the line count: a fixed set of
      // streamlines that converge at drifting merge nodes and diverge after
      // (wood-grain / dendrite braid). Passed to weaveDispY per control point.
      float density = mpOp ? mpOp->vizDensity() : 0.5f;
      if (density < 0.0f) density = 0.0f; else if (density > 1.0f) density = 1.0f;
      const int n = field::kStreamN;

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
      // Bridge the 1px SpottedStrip gap: every ply but the last draws one extra
      // content column (px = left+w) so the continuous curve crosses into the
      // next ply with no hairline seam. The last ply stops at its own edge.
      const int wext = (mIndex < mCount - 1) ? 1 : 0;
      // Per-BAND renderer: both lines of band b PLUS the negative space between
      // them FILLED with background, so the band occludes bubbles drawn behind it
      // (lower z). Invoked in z-order -> bubbles weave through the bands.
      auto renderBand = [&](int b)
      {
        const int s0 = 2 * b, s1 = 2 * b + 1;
        const float yb0 = ((float)s0 + 0.5f) * (float)h / (float)n;
        const float yb1 = ((float)s1 + 0.5f) * (float)h / (float)n;
        float cY0[40], cB0[40], cY1[40], cB1[40];
        for (int i = 0; i < mctrl; i++)
        {
          const float cx = (float)((g0 + i) * cstep);
          float y0 = yb0 + field::flow(cx, yb0, phase);
          float y1 = yb1 + field::flow(cx, yb1, phase);
          float gl0 = 0.0f, gl1 = 0.0f;
          for (int d = 0; d < nd; d++)
          {
            const field::RippleHit a0 = field::rippleEval(cx - dX[d], y0 - dY[d], dAge[d], dC[d]);
            y0 += dAmp[d] * a0.bend; gl0 += dAmp[d] * a0.glow;
            const field::RippleHit a1 = field::rippleEval(cx - dX[d], y1 - dY[d], dAge[d], dC[d]);
            y1 += dAmp[d] * a1.bend; gl1 += dAmp[d] * a1.glow;
          }
          cY0[i] = y0; cB0[i] = gl0; cY1[i] = y1; cB1[i] = gl1;
        }
        float prev0 = -1000.0f, prev1 = -1000.0f;
        for (int lx = 0; lx < w + wext; lx++)
        {
          const int cx = x0 + lx;
          const int seg = cx / cstep;
          const int idx = seg - g0;
          const float t = (float)(cx - seg * cstep) / (float)cstep;
          float y0 = field::catmull(cY0[idx - 1], cY0[idx], cY0[idx + 1], cY0[idx + 2], t);
          float y1 = field::catmull(cY1[idx - 1], cY1[idx], cY1[idx + 1], cY1[idx + 2], t);
          float gl0 = field::catmull(cB0[idx - 1], cB0[idx], cB0[idx + 1], cB0[idx + 2], t);
          float gl1 = field::catmull(cB1[idx - 1], cB1[idx], cB1[idx + 1], cB1[idx + 2], t);
          if (y0 < 0.0f) y0 = 0.0f; else if (y0 > (float)(h - 1)) y0 = (float)(h - 1);
          if (y1 < 0.0f) y1 = 0.0f; else if (y1 > (float)(h - 1)) y1 = (float)(h - 1);
          const int px = left + lx;
          // Fill the negative space between the pair with background -> occlude.
          const int flo = (int)(y0 < y1 ? y0 : y1) + 1;
          const int fhi = (int)(y0 < y1 ? y1 : y0);
          for (int yy = flo; yy < fhi; yy++) fb.pixel(0, px, bot + yy);
          drawLinePix(fb, px, bot, h, y0, gl0, baseB, prev0);
          drawLinePix(fb, px, bot, h, y1, gl1, baseB, prev1);
        }
      };

      // Bubbles (Density): outlined circles, 2 levels brighter than the lines
      // across the Mix throw, CLIPPED to this ply's window (a strip-spanning
      // bubble is drawn in pieces by each ply -> correct per-ply z-order, no
      // cross-ply clobber). Midpoint-circle outline.
      int bubB = (int)(baseB + 2.5f);
      if (bubB > 15) bubB = 15;
      const int bXlo = left, bXhi = left + w + wext, bYlo = bot, bYhi = bot + h;
      auto drawBubble = [&](float bx, float by, float br)
      {
        const int cxp = left + (int)(bx - (float)x0 + 0.5f);
        const int cyp = bot + (int)(by + 0.5f);
        const int rad = (int)(br + 0.5f);
        // Fill the interior with background -> occlude lower-z material (bands /
        // bubbles drawn behind it). Clipped to the ply window.
        for (int dy = -rad; dy <= rad; dy++)
        {
          const int py = cyp + dy;
          if (py < bYlo || py >= bYhi) continue;
          const int dx = (int)(sqrtf((float)(rad * rad - dy * dy)) + 0.5f);
          int xa = cxp - dx; if (xa < bXlo) xa = bXlo;
          int xb = cxp + dx; if (xb >= bXhi) xb = bXhi - 1;
          for (int px = xa; px <= xb; px++) fb.pixel(0, px, py);
        }
        // Bright outline on top.
        int xx = rad, yy = 0, err = 1 - rad;
        while (xx >= yy)
        {
          plotClip(fb, cxp + xx, cyp + yy, bubB, bXlo, bXhi, bYlo, bYhi);
          plotClip(fb, cxp + yy, cyp + xx, bubB, bXlo, bXhi, bYlo, bYhi);
          plotClip(fb, cxp - yy, cyp + xx, bubB, bXlo, bXhi, bYlo, bYhi);
          plotClip(fb, cxp - xx, cyp + yy, bubB, bXlo, bXhi, bYlo, bYhi);
          plotClip(fb, cxp - xx, cyp - yy, bubB, bXlo, bXhi, bYlo, bYhi);
          plotClip(fb, cxp - yy, cyp - xx, bubB, bXlo, bXhi, bYlo, bYhi);
          plotClip(fb, cxp + yy, cyp - xx, bubB, bXlo, bXhi, bYlo, bYhi);
          plotClip(fb, cxp + xx, cyp - yy, bubB, bXlo, bXhi, bYlo, bYhi);
          yy++;
          if (err < 0) err += 2 * yy + 1;
          else { xx--; err += 2 * (yy - xx) + 1; }
        }
      };

      // Z-ORDER COMPOSITE: bands (line-pairs, randomized z per unit) + bubbles,
      // drawn back->front so bubbles weave in front of / behind the bands by z.
      // Band b = lines 2b, 2b+1 at z = vizBandZ(b); nBands = n/2.
      const int nBands = n / 2;
      struct ZItem { float z; int type; int idx; float bx, by, br; };
      ZItem items[field::kStreamN / 2 + kVizMaxBubbles];
      int ni = 0;
      for (int b = 0; b < nBands; b++)
      {
        items[ni].z = mpOp ? (float)mpOp->vizBandZ(b) : (float)b;
        items[ni].type = 0;
        items[ni].idx = b;
        ni++;
      }
      if (mpOp)
      {
        for (int i = 0; i < kVizMaxBubbles; i++)
        {
          const float br = mpOp->vizBubR(i);
          if (br <= 0.0f) continue;
          items[ni].z = mpOp->vizBubZ(i);
          items[ni].type = 1;
          items[ni].bx = mpOp->vizBubX(i);
          items[ni].by = mpOp->vizBubY(i);
          items[ni].br = br;
          ni++;
        }
      }
      for (int a = 1; a < ni; a++) // insertion sort ascending z (back -> front)
      {
        ZItem key = items[a];
        int j = a - 1;
        while (j >= 0 && items[j].z > key.z) { items[j + 1] = items[j]; j--; }
        items[j + 1] = key;
      }
      for (int it = 0; it < ni; it++)
      {
        if (items[it].type == 0)
          renderBand(items[it].idx);
        else
          drawBubble(items[it].bx, items[it].by, items[it].br);
      }

      // Impact splashes (front-most): rain flecks for drops in this ply's window.
      drawLooper(fb, left, bot, w, x0);
    }

    // ---- feature renderers ----

    // One streamline pixel column: sqrt-AA core + solid gap-fill for continuity.
    void drawLinePix(od::FrameBuffer &fb, int px, int bot, int h, float y, float glow, float baseB, float &prevY)
    {
      float bf = baseB + glow * field::kGlowGain;
      if (bf < 0.0f) bf = 0.0f; else if (bf > 15.0f) bf = 15.0f;
      const int bri = (int)(bf + 0.5f);
      const int yi = (int)y;
      const float f = y - (float)yi;
      fb.pixel((int)(bri * sqrtf(1.0f - f) + 0.5f), px, bot + yi);
      if (yi + 1 < h) fb.pixel((int)(bri * sqrtf(f) + 0.5f), px, bot + yi + 1);
      if (prevY > -999.0f)
      {
        const int a = (int)(prevY < y ? prevY : y);
        const int b = (int)(prevY < y ? y : prevY);
        for (int yy = a + 1; yy < b; yy++) fb.pixel(bri, px, bot + yy);
      }
      prevY = y;
    }

    // Plot a pixel only inside the clip window (bubbles are clipped to their ply).
    void plotClip(od::FrameBuffer &fb, int px, int py, int color, int xlo, int xhi, int ylo, int yhi)
    {
      if (px >= xlo && px < xhi && py >= ylo && py < yhi) fb.pixel(color, px, py);
    }

    // A growing dendrite branch from (ox,oy) toward the neighbour line (+gap),
    // a quadratic-bezier curve drawn only to fraction g (the grown extent), with
    // a soft tip fade. seed varies lean/bow for an organic look.
    void drawBranch(od::FrameBuffer &fb, float ox, float oy, float gap, float g, int seed, float bright)
    {
      const float lean = (seed & 1) ? field::kConnLean : -field::kConnLean;
      const float tx = ox + lean, ty = oy + gap; // target ~ neighbour line
      const float cx = (ox + tx) * 0.5f + ((seed & 2) ? field::kConnBow : -field::kConnBow);
      const float cy = (oy + ty) * 0.5f;
      const int steps = 8;
      int ppx = -10000, ppy = 0;
      for (int i = 1; i <= steps; i++)
      {
        const float t = g * (float)i / (float)steps; // grow up to fraction g
        const float u = 1.0f - t;
        const float bx = u * u * ox + 2.0f * u * t * cx + t * t * tx;
        const float by = u * u * oy + 2.0f * u * t * cy + t * t * ty;
        float tip = (g - t) * 6.0f; // fade the growing tip
        if (tip < 0.0f) tip = 0.0f; else if (tip > 1.0f) tip = 1.0f;
        int c = (int)(bright * tip + 0.5f);
        if (c > 15) c = 15;
        const int px = (int)(bx + 0.5f), py = (int)(by + 0.5f);
        if (ppx > -10000 && c > 0) fb.line(c, ppx, ppy, px, py);
        ppx = px; ppy = py;
      }
    }

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
