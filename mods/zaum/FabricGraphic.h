#pragma once

#include <od/graphics/Graphic.h>
#include "atoms/APFTank.h"
#include <string.h>

namespace zaum
{
  // 2.5D "fabric" overview: a stack of serial lines (an orthographic
  // parallelogram, no perspective) whose FRONT edge is the decimated mono-wet
  // snapshot from the reverb; older rows scroll back-and-up and are depth-dimmed,
  // so you watch the wet ripple across the sheet and recede. Freeze holds the
  // scroll (the fabric freezes mid-ripple and gently breathes). A lateral
  // smoothing pass gives the connected-membrane feel.
  //
  // HEADER-ONLY, ALL VIRTUALS INLINE (GCC key-function / am335x insert-crash
  // rule - see docs/graphics-authoring-guide.md). Orthographic projection uses
  // only linear offsets (no sinf/cosf), sidestepping the package-trig trap; the
  // tiny breathe uses an inline polynomial, not libm.
  class FabricGraphic : public od::Graphic
  {
  public:
    FabricGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpTank(0) {}

    virtual ~FabricGraphic()
    {
      if (mpTank)
        mpTank->release();
    }

    void follow(APFTank *p)
    {
      if (mpTank)
        mpTank->release();
      mpTank = p;
      if (mpTank)
        mpTank->attach();
    }

    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth, h = mHeight;
      int left = mWorldLeft, bot = mWorldBottom;

      // Real clear (fb.fill(BLACK,...) is an OR of 0 -> a no-op).
      fb.clear(left, bot, left + w - 1, bot + h - 1);

      if (!mpTank)
        return;

      if (!mInit)
      {
        memset(mHist, 0, sizeof(mHist));
        mInit = true;
      }

      const int cols = kCols; // must match APFTank::kVizCols
      float fz = mpTank->vizFreeze();

      // Scroll gate: advance ~ (1 - freeze), so Freeze holds the sheet.
      mScrollAcc += (1.0f - fz) * kScrollRate + 0.0001f; // tiny floor keeps it alive
      if (mScrollAcc >= 1.0f)
      {
        mScrollAcc -= 1.0f;
        for (int r = kRows - 1; r > 0; r--)
          for (int c = 0; c < cols; c++)
            mHist[r][c] = mHist[r - 1][c];
        // New front row: decimated wet, laterally smoothed toward a membrane.
        for (int c = 0; c < cols; c++)
        {
          float s0 = mpTank->vizSample(c);
          float sl = mpTank->vizSample(c > 0 ? c - 1 : 0);
          float sr = mpTank->vizSample(c < cols - 1 ? c + 1 : cols - 1);
          float sm = 0.25f * (sl + 2.0f * s0 + sr);
          mHist[0][c] = kMembrane * sm + (1.0f - kMembrane) * s0;
        }
      }

      // Subtle breathe when frozen so the held sheet stays alive.
      mBreathe += 0.03f;
      if (mBreathe > 6.28318531f)
        mBreathe -= 6.28318531f;
      float vscale = (float)h * 0.28f * (1.0f + 0.06f * fz * fastSin(mBreathe));

      // Orthographic projection (linear, no trig): rows step up-and-right as they
      // recede -> a parallelogram; front bright, back dim.
      const int marginX = 3, marginY = 4;
      const float rowSkewX = 2.0f;
      const float rowSkewY = 2.2f;
      float usableW = (float)(w - 2 * marginX) - rowSkewX * (float)(kRows - 1);
      if (usableW < 1.0f)
        usableW = 1.0f;
      float colStep = usableW / (float)(cols - 1);
      float baseY = (float)bot + (float)marginY;
      int top = bot + h - 1;
      int right = left + w - 1;

      // Back-to-front so the bright near rows overwrite the dim far ones.
      for (int r = kRows - 1; r >= 0; r--)
      {
        int gray = 14 - (r * 11) / kRows;
        if (gray < 3)
          gray = 3;
        float rowX = (float)(left + marginX) + rowSkewX * (float)r;
        float rowY = baseY + rowSkewY * (float)r;
        int prevSx = -1, prevSy = 0;
        for (int c = 0; c < cols; c++)
        {
          int sx = (int)(rowX + colStep * (float)c);
          int sy = (int)(rowY + mHist[r][c] * vscale);
          if (sx < left) sx = left; else if (sx > right) sx = right;
          if (sy < bot) sy = bot; else if (sy > top) sy = top;
          if (prevSx >= 0)
            fb.line(gray, prevSx, prevSy, sx, sy);
          prevSx = sx;
          prevSy = sy;
        }
      }
    }

  private:
    APFTank *mpTank;
    static const int kRows = 12;
    static const int kCols = 32; // matches APFTank::kVizCols
    static constexpr float kScrollRate = 1.0f;  // rows advanced per frame at freeze=0
    static constexpr float kMembrane = 0.65f;    // 0 = crisp lines, 1 = smooth membrane
    float mHist[kRows][kCols];
    bool mInit = false;
    float mScrollAcc = 0.0f;
    float mBreathe = 0.0f;

    // Inline polynomial sine for the breathe (no libm; arg wrapped to [-pi,pi]).
    static inline float fastSin(float x)
    {
      if (x > 3.14159265f)
        x -= 6.28318531f;
      float x2 = x * x;
      return x * (1.0f - x2 * (0.16666667f - x2 * 0.00833333f));
    }
  };
}
