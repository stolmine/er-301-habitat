// CanalsRoutingGraphic — overview ply main graphic for Canals.
//
// Visualizes the routing state of the unit's three per-block inputs.
// The ply main area (width × 64) is divided into three horizontal
// stripes (LOW / CENTRE / HIGH from top to bottom). Each stripe
// renders a small scope of the post-routing input waveform feeding
// that filter block.
//
// When a block is sourcing from ALL fallback (no per-block signal
// patched), the word "ALL" is overlaid on its stripe in a small
// 3×5 pixel font. Per-pixel inversion against the underlying scope
// intensity keeps the text legible regardless of waveform density.
//
// A small L / C / H signifier in the top-right corner of each stripe
// indicates which filter block the stripe belongs to.
//
// Header-only per feedback_no_out_of_line_virtuals. Per-pixel writes
// only (no line() / fill()) per feedback_framebuffer_blend_vs_set.

#pragma once

#include "Canals.h"
#include <od/graphics/Graphic.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  class CanalsRoutingGraphic : public od::Graphic
  {
  public:
    CanalsRoutingGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpCanals(0) {}

    virtual ~CanalsRoutingGraphic()
    {
      if (mpCanals)
        mpCanals->release();
    }

    void follow(Canals *p)
    {
      if (mpCanals)
        mpCanals->release();
      mpCanals = p;
      if (mpCanals)
        mpCanals->attach();
    }

  private:
    Canals *mpCanals;

    // Internal intensity buffer. Scope is plotted here per-pixel,
    // then "ALL" / corner letters overlay with per-pixel inversion
    // lookup. Blitted to framebuffer at end of draw().
    static const int kMaxW = 48;
    static const int kMaxH = 64;
    uint8_t mPixels[kMaxW * kMaxH];

    // 3×5 pixel glyphs — bits laid MSB-first across the 3-px row.
    // Just the letters used: A, L, C, H.
    static const uint8_t *getGlyph(char c)
    {
      static const uint8_t glyphA[5] = { 0b010, 0b101, 0b111, 0b101, 0b101 };
      static const uint8_t glyphL[5] = { 0b100, 0b100, 0b100, 0b100, 0b111 };
      static const uint8_t glyphC[5] = { 0b111, 0b100, 0b100, 0b100, 0b111 };
      static const uint8_t glyphH[5] = { 0b101, 0b101, 0b111, 0b101, 0b101 };
      switch (c) {
        case 'A': return glyphA;
        case 'L': return glyphL;
        case 'C': return glyphC;
        case 'H': return glyphH;
      }
      return 0;
    }

    // Plot a glyph with INVERSION: each lit pixel of the glyph
    // looks up the underlying intensity in mPixels and writes the
    // contrasting value (0 if underlying ≥ 8, otherwise 15).
    //
    // Y-up framebuffer: glyph row 0 (top of letter) must map to a
    // HIGHER y in the intensity buffer. Hence the (4 - row) flip.
    inline void plotGlyphInverted(char c, int x, int y, int w, int h)
    {
      const uint8_t *g = getGlyph(c);
      if (!g) return;
      for (int row = 0; row < 5; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 3; col++) {
          if (bits & (1 << (2 - col))) {
            int px = x + col;
            int py = y + (4 - row);  // flip for Y-up framebuffer
            if (px < 0 || px >= w) continue;
            if (py < 0 || py >= h) continue;
            uint8_t under = mPixels[py * w + px];
            mPixels[py * w + px] = (under >= 8) ? 0 : 15;
          }
        }
      }
    }

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth  < kMaxW ? mWidth  : kMaxW;
      int h = mHeight < kMaxH ? mHeight : kMaxH;

      memset(mPixels, 0, sizeof(mPixels));

      if (mpCanals) {
        const int stripeH = h / 3;

        for (int block = 0; block < 3; block++) {
          // Y-up framebuffer: block 0 (LOW) at low Y = bottom of
          // screen; block 2 (HIGH) at high Y = top of screen. The
          // block index maps directly to display position; no flip
          // needed.
          int y0 = block * stripeH;
          int y1 = (block == 2) ? (h - 1) : (y0 + stripeH - 1);
          int centerY = (y0 + y1) / 2;
          int amplitude = (y1 - y0) / 2 - 1;
          if (amplitude < 1) amplitude = 1;

          // Auto-scale: track peak |sample| across the ring so the
          // scope fills the stripe regardless of signal level. Floor
          // prevents silent stripes from amplifying noise.
          float peak = 0.01f;
          for (int i = 0; i < 256; i++) {
            float s = mpCanals->getBlockInputSample(block, i);
            if (!(s == s)) continue;
            float a = (s < 0.0f) ? -s : s;
            if (a > peak) peak = a;
          }
          float invPeak = 1.0f / peak;

          // MiniScope-style min/max bar rendering. For each column,
          // find the min and max sample in the corresponding ring
          // buffer range and draw a vertical fill from min to max
          // plus peak markers at the extremes. Smoother and far more
          // informative than single-pixel-per-column sampling.
          for (int x = 0; x < w; x++) {
            int idx0 = (x * 256) / w;
            int idx1 = ((x + 1) * 256) / w;
            if (idx1 > 256) idx1 = 256;
            if (idx1 <= idx0) idx1 = idx0 + 1;

            float minS = 0.0f, maxS = 0.0f;
            bool first = true;
            for (int i = idx0; i < idx1; i++) {
              float s = mpCanals->getBlockInputSample(block, i);
              if (!(s == s) || s > 1e6f || s < -1e6f) continue;
              if (first) { minS = s; maxS = s; first = false; }
              else {
                if (s < minS) minS = s;
                if (s > maxS) maxS = s;
              }
            }

            float minN = minS * invPeak;
            float maxN = maxS * invPeak;
            if (minN < -1.0f) minN = -1.0f;
            if (maxN >  1.0f) maxN =  1.0f;

            // Note Y inversion: higher sample value = higher Y in
            // screen-up convention.
            int yTop    = centerY + (int)(maxN * (float)amplitude);
            int yBottom = centerY + (int)(minN * (float)amplitude);
            if (yTop    > y1) yTop    = y1;
            if (yBottom < y0) yBottom = y0;
            if (yTop < yBottom) { int t = yTop; yTop = yBottom; yBottom = t; }

            // Mid-gray fill from bottom to top of the per-column bar.
            for (int yy = yBottom; yy <= yTop; yy++) {
              if (mPixels[yy * w + x] < 7) mPixels[yy * w + x] = 7;
            }
            // Bright peak markers at top + bottom.
            mPixels[yTop    * w + x] = 14;
            if (yBottom != yTop)
              mPixels[yBottom * w + x] = 14;
          }

          // "ALL" overlay if block is using ALL fallback. 3-char
          // total width = 3*3 chars + 2 inter-char gaps = 11 px.
          if (mpCanals->isBlockUsingAll(block)) {
            int textW = 11;
            int textH = 5;
            int tx = (w - textW) / 2;
            // y arg is BOTTOM of glyph (lowest Y); glyph spans 5 rows
            // upward from there.
            int ty = centerY - textH / 2;
            plotGlyphInverted('A', tx,     ty, w, h);
            plotGlyphInverted('L', tx + 4, ty, w, h);
            plotGlyphInverted('L', tx + 8, ty, w, h);
          }

          // Corner signifier (L / C / H) overlaid on the scope in the
          // upper-right corner of each stripe, per-pixel inversion
          // for visibility. y arg is BOTTOM of glyph; glyph top will
          // land at y1.
          static const char kBlockLetter[3] = {'L', 'C', 'H'};
          int cx = w - 4;
          int cy = y1 - 4;
          plotGlyphInverted(kBlockLetter[block], cx, cy, w, h);
        }
      }

      // Background + blit. Per feedback_framebuffer_blend_vs_set,
      // only pixel() — no line/fill on top of the intensity buffer.
      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          uint8_t v = mPixels[y * w + x];
          if (v > 0)
            fb.pixel(v, mWorldLeft + x, mWorldBottom + y);
        }
      }
    }
  };

} // namespace stolmine
