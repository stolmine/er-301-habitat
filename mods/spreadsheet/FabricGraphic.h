#pragma once

#include <od/graphics/Graphic.h>
#include "APFTank.h"
#include <string.h>
#include <math.h>

namespace stolmine
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

      // Keep the DSP-side spectrum analyzer alive: it idles when we stop drawing.
      mpTank->vizPing();

      if (!mInit)
      {
        memset(mHist, 0, sizeof(mHist));
        memset(mFront, 0, sizeof(mFront));
        mInit = true;
      }

      const int cols = kCols; // must match APFTank::kVizCols

      // --- Front line: read the box-averaged wet, DC-remove, and heavily SLEW
      //     each column across frames (Helicase's actual smoothing). Runs every
      //     frame so the incoming contour is smooth, not jittery.
      // sqrtf = log-ish amplitude compression so loud low bands don't dominate;
      // bandTilt additionally down-weights the low frequencies.
      float mean = 0.0f;
      for (int c = 0; c < cols; c++)
        mean += sqrtf(mpTank->vizSample(c)) * bandTilt(c);
      mean *= (1.0f / (float)cols);
      mDc += (mean - mDc) * 0.1f;
      float pk = 0.0f;
      for (int c = 0; c < cols; c++)
      {
        float v = sqrtf(mpTank->vizSample(c)) * bandTilt(c) - mDc;
        mFront[c] += (v - mFront[c]) * kSlew;
        float a = mFront[c] < 0.0f ? -mFront[c] : mFront[c];
        if (a > pk) pk = a;
      }
      // Auto-scale: track the peak (fast attack, slow release) so the fabric fills
      // the ply regardless of input level; below a floor -> flat (silence).
      mPeak += (pk - mPeak) * (pk > mPeak ? 0.3f : 0.03f);

      // Scroll one row per frame (Freeze no longer affects the viz). Shift history
      // back, insert the laterally smoothed front line.
      mScrollAcc += kScrollRate;
      if (mScrollAcc >= 1.0f)
      {
        mScrollAcc -= 1.0f;
        for (int r = kRows - 1; r > 0; r--)
          for (int c = 0; c < cols; c++)
            mHist[r][c] = mHist[r - 1][c];
        for (int c = 0; c < cols; c++)
        {
          float l = mFront[c > 0 ? c - 1 : 0];
          float rr = mFront[c < cols - 1 ? c + 1 : cols - 1];
          float sm = 0.25f * (l + 2.0f * mFront[c] + rr);
          mHist[0][c] = kMembrane * sm + (1.0f - kMembrane) * mFront[c];
        }
      }

      // Full-overhead layout: each contour is a FLAT horizontal line (the spectrum
      // across the width); contours stack VERTICALLY (newest at the bottom, older
      // rows scroll up and dim). No perspective skew - fills the whole ply.
      const int marginX = 1, marginY = 2;
      float colStep = (float)(w - 2 * marginX) / (float)(cols - 1);
      float rowSpacing = (float)(h - 2 * marginY) / (float)(kRows - 1);
      float originX = (float)left + (float)marginX;
      float originY = (float)bot + (float)marginY;
      // Ripple: auto-scaled to ~row spacing so lines wiggle and lightly weave
      // without turning to mush; below a floor -> flat (silence).
      float vscale = (mPeak > 1e-4f) ? (rowSpacing * 0.9f / mPeak) : 0.0f;
      int top = bot + h - 1;
      int right = left + w - 1;

      // Back-to-front so the bright near rows overwrite the dim far ones. Each row
      // is a Catmull-Rom curve through its column samples (kSub segments/span).
      for (int r = kRows - 1; r >= 0; r--)
      {
        int gray = 14 - (r * 11) / kRows;
        if (gray < 3)
          gray = 3;
        float rowX = originX;                          // flat: no horizontal skew
        float rowY = originY + rowSpacing * (float)r;  // stacked vertically
        int prevSx = -1, prevSy = 0;
        for (int c = 0; c < cols - 1; c++)
        {
          int i0 = c > 0 ? c - 1 : 0;
          int i2 = c + 1;
          int i3 = c + 2 < cols ? c + 2 : cols - 1;
          for (int s = 0; s < kSub; s++)
          {
            float tt = (float)s / (float)kSub;
            float val = catmullRom(mHist[r][i0], mHist[r][c], mHist[r][i2],
                                   mHist[r][i3], tt, 0.5f);
            int sx = (int)(rowX + colStep * ((float)c + tt));
            int sy = (int)(rowY + val * vscale);
            if (sx < left) sx = left; else if (sx > right) sx = right;
            if (sy < bot) sy = bot; else if (sy > top) sy = top;
            if (prevSx >= 0)
              fb.line(gray, prevSx, prevSy, sx, sy);
            prevSx = sx;
            prevSy = sy;
          }
        }
        // final column endpoint
        int sx = (int)(rowX + colStep * (float)(cols - 1));
        int sy = (int)(rowY + mHist[r][cols - 1] * vscale);
        if (sx < left) sx = left; else if (sx > right) sx = right;
        if (sy < bot) sy = bot; else if (sy > top) sy = top;
        if (prevSx >= 0)
          fb.line(gray, prevSx, prevSy, sx, sy);
      }
    }

  private:
    APFTank *mpTank;
    static const int kRows = 16; // flat contours stacked to fill the ply height
    static const int kCols = 16; // matches APFTank::kVizCols
    static constexpr float kScrollRate = 1.0f;  // rows advanced per frame at freeze=0
    static constexpr float kMembrane = 0.6f;     // 0 = crisp lines, 1 = smooth membrane
    static constexpr float kSlew = 0.15f;        // per-column temporal slew (Helicase-style)
    float mHist[kRows][kCols];
    float mFront[kCols];   // slewed, DC-removed front line (class member, not stack)
    float mDc = 0.0f;      // slow DC tracker
    float mPeak = 0.0f;    // running peak for auto-scale (fast attack / slow release)
    bool mInit = false;
    float mScrollAcc = 0.0f;

    // Catmull-Rom (Hermite form, tangents = tau*(p2-p0)/(p3-p1)) for smooth row
    // curves between the column samples. (Same helper as HelicaseOrbitalGraphic.)
    static inline float catmullRom(float p0, float p1, float p2, float p3,
                                   float t, float tau)
    {
      float t2 = t * t, t3 = t2 * t;
      float m1 = tau * (p2 - p0);
      float m2 = tau * (p3 - p1);
      return (2.0f * t3 - 3.0f * t2 + 1.0f) * p1 + (t3 - 2.0f * t2 + t) * m1 +
             (-2.0f * t3 + 3.0f * t2) * p2 + (t3 - t2) * m2;
    }
    static const int kSub = 3; // Catmull-Rom sub-segments per column span

    // Per-band display tilt: reverb spectra are bass-heavy, so down-weight the
    // low bands and up-weight the highs (a rising weight across frequency).
    static constexpr float kTiltLo = 0.25f; // weight of band 0 (bass)
    static constexpr float kTiltHi = 1.3f;  // weight of the top band (treble)
    static inline float bandTilt(int c)
    {
      return kTiltLo + (kTiltHi - kTiltLo) * (float)c / (float)(kCols - 1);
    }
  };
}
