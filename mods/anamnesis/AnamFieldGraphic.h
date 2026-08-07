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
    // draw() itself MUST stay inline (framework-virtual rule,
    // feedback_no_out_of_line_virtuals / check-graphic-virtual-defs.sh);
    // it delegates to the non-virtual drawImpl, defined out-of-line in
    // Anamnesis.cpp so the heavy raster path compiles at the package speed
    // profile instead of the -Os SWIG-wrapper profile. Non-virtuals do not
    // create a key function, so the vtable stays COMDAT-inline.
    virtual void draw(od::FrameBuffer &fb) { drawImpl(fb); }
    void drawImpl(od::FrameBuffer &fb);

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

    // Clipped Bresenham line (blob outlines may cross the ply boundary).
    void drawLineClip(od::FrameBuffer &fb, int x0, int y0, int x1, int y1, int color,
                      int xlo, int xhi, int ylo, int yhi)
    {
      int dx = x1 - x0; if (dx < 0) dx = -dx;
      int dy = y1 - y0; if (dy < 0) dy = -dy; dy = -dy; // dy <= 0
      const int sx = x0 < x1 ? 1 : -1;
      const int sy = y0 < y1 ? 1 : -1;
      int err = dx + dy;
      for (;;)
      {
        if (x0 >= xlo && x0 < xhi && y0 >= ylo && y0 < yhi) fb.pixel(color, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
      }
    }

    void aaPlot(od::FrameBuffer &fb, int px, int py, float cov, int color,
                int xlo, int xhi, int ylo, int yhi)
    {
      if (cov <= 0.0f || px < xlo || px >= xhi || py < ylo || py >= yhi) return;
      int c = (int)((float)color * cov + 0.5f);
      if (c > 15) c = 15;
      if (c > 0) fb.pixel(c, px, py);
    }

    // Anti-aliased clipped line (Wu-style) -> the iso-contour edge moves fluidly
    // sub-pixel instead of jumping a whole pixel as the field morphs.
    void drawAALineClip(od::FrameBuffer &fb, float x0, float y0, float x1, float y1,
                        int color, int xlo, int xhi, int ylo, int yhi)
    {
      float dx = x1 - x0, dy = y1 - y0;
      const bool steep = (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx);
      if (steep) { float t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
      if (x0 > x1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
      dx = x1 - x0; dy = y1 - y0;
      const float grad = (dx == 0.0f) ? 0.0f : dy / dx;
      const int ix0 = (int)(x0 + 0.5f), ix1 = (int)(x1 + 0.5f);
      float y = y0 + grad * ((float)ix0 - x0);
      for (int x = ix0; x <= ix1; x++)
      {
        int iy = (int)y; if ((float)iy > y) iy--; // exact floor, no libm call
        const float fy = y - (float)iy;
        if (steep)
        {
          aaPlot(fb, iy, x, 1.0f - fy, color, xlo, xhi, ylo, yhi);
          aaPlot(fb, iy + 1, x, fy, color, xlo, xhi, ylo, yhi);
        }
        else
        {
          aaPlot(fb, x, iy, 1.0f - fy, color, xlo, xhi, ylo, yhi);
          aaPlot(fb, x, iy + 1, fy, color, xlo, xhi, ylo, yhi);
        }
        y += grad;
      }
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
    // (Metaball field grid moved to Anamnesis::mFcGrid -- one shared strip-wide
    // grid built once per frame on the op, not a per-ply slew grid. Item 1.)
  };

} // namespace anamnesis
