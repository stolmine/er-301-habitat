// MirrorPhosphorGraphic — Y-axis reflected phase-space phosphor scope
// for the Mirror unit's overview ply. Header-only (per
// feedback_no_out_of_line_virtuals); modeled directly on Rauschen's
// PhaseSpaceGraphic with two design departures:
//
//   1. No 3D tumble. Static 2D phase portrait preserves the mirror
//      identity — rotation would scramble the symmetry.
//   2. Y-axis reflection at plot time. Every (px, py) is also plotted
//      at ((w-1)-px, py). Image is always left-right symmetric about
//      the vertical centerline.
//
// Source signal: Mirror::getOutputSample(idx) — decimated L output
// post-crusher, post-DC, post-soft-clip. Plot 254 sample pairs per
// draw call as x[n] vs x[n+1] phase space.
//
// Phosphor mechanics match Rauschen: 64×64 uint8 buffer, fade -1 per
// draw call (~200 ms decay at 40 fps), increment +4 per hit, cap at 12.
//
// See planning/mirror-phosphor-viz-plan.md for the full design.

#pragma once

#include "Mirror.h"
#include <od/graphics/Graphic.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  class MirrorPhosphorGraphic : public od::Graphic
  {
  public:
    MirrorPhosphorGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpMirror(0) {}

    virtual ~MirrorPhosphorGraphic()
    {
      if (mpMirror)
        mpMirror->release();
    }

    void follow(Mirror *p)
    {
      if (mpMirror)
        mpMirror->release();
      mpMirror = p;
      if (mpMirror)
        mpMirror->attach();
    }

  private:
    Mirror *mpMirror;

    static const int kMaxW = 96;
    static const int kMaxH = 64;
    uint8_t mPixels[kMaxW * kMaxH];
    bool    mCleared = false;

    float mScaleMin = -1.0f;
    float mScaleMax =  1.0f;

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth  < kMaxW ? mWidth  : kMaxW;
      int h = mHeight < kMaxH ? mHeight : kMaxH;

      if (!mCleared) {
        memset(mPixels, 0, sizeof(mPixels));
        mCleared = true;
      }

      // Phosphor decay — every pixel fades one level per frame.
      for (int i = 0; i < w * h; i++) {
        if (mPixels[i] > 0) mPixels[i]--;
      }

      if (mpMirror) {
        // Auto-scale: track min/max across the ring buffer with
        // smoothed expand/contract (Rauschen pattern). Fast expand
        // catches new peaks; slow contract avoids breathing.
        float curMin =  1e10f;
        float curMax = -1e10f;
        for (int i = 0; i < 256; i++) {
          float s = mpMirror->getOutputSample(i);
          if (!(s == s) || s > 1e6f || s < -1e6f) continue;
          if (s < curMin) curMin = s;
          if (s > curMax) curMax = s;
        }
        if (curMin > curMax) { curMin = -1.0f; curMax = 1.0f; }

        const float kExpand   = 0.5f;
        const float kContract = 0.02f;
        if (curMin < mScaleMin) mScaleMin += (curMin - mScaleMin) * kExpand;
        else                    mScaleMin += (curMin - mScaleMin) * kContract;
        if (curMax > mScaleMax) mScaleMax += (curMax - mScaleMax) * kExpand;
        else                    mScaleMax += (curMax - mScaleMax) * kContract;

        if (!(mScaleMin == mScaleMin)) mScaleMin = -1.0f;
        if (!(mScaleMax == mScaleMax)) mScaleMax =  1.0f;
        if (mScaleMax <= mScaleMin)    mScaleMax  = mScaleMin + 0.01f;
        float range = mScaleMax - mScaleMin;
        if (range < 0.01f) {
          float mid = (mScaleMin + mScaleMax) * 0.5f;
          mScaleMin = mid - 0.005f;
          mScaleMax = mid + 0.005f;
          range = 0.01f;
        }
        float invRange = 1.0f / range;

        // Plot 254 consecutive sample pairs as 2D phase space.
        // For each (s0, s1), light up (px, py) AND its Y-axis
        // reflection ((w-1)-px, py). Image is left-right symmetric
        // about the vertical centerline — the mirror.
        for (int i = 0; i < 255; i++) {
          float s0 = mpMirror->getOutputSample(i);
          float s1 = mpMirror->getOutputSample(i + 1);
          if (!(s0 == s0) || !(s1 == s1)) continue;

          float nx = (s0 - mScaleMin) * invRange;
          float ny = (s1 - mScaleMin) * invRange;

          // Reflective fold instead of clamp: overshoot mirrors back
          // into [0, 1] symmetrically. Bounded iteration handles
          // extreme overshoot (rare; auto-scale catches most peaks
          // within a few frames). Conceptually matches the Mirror
          // block's mechanic — above-bound content folds back into
          // the visible field instead of pinning at the edge.
          for (int j = 0; j < 8; j++) {
            if (nx > 1.0f)      nx = 2.0f - nx;
            else if (nx < 0.0f) nx = -nx;
            else break;
          }
          for (int j = 0; j < 8; j++) {
            if (ny > 1.0f)      ny = 2.0f - ny;
            else if (ny < 0.0f) ny = -ny;
            else break;
          }

          int px = (int)(nx * (float)(w - 1));
          int py = (int)(ny * (float)(h - 1));
          if (px < 0)   px = 0;
          if (px >= w)  px = w - 1;
          if (py < 0)   py = 0;
          if (py >= h)  py = h - 1;

          int idxA = py * w + px;
          int b = mPixels[idxA] + 4;
          if (b > 12) b = 12;
          mPixels[idxA] = (uint8_t)b;

          // Y-axis reflection: mirror x around the vertical centerline.
          int pxR = (w - 1) - px;
          int idxB = py * w + pxR;
          b = mPixels[idxB] + 4;
          if (b > 12) b = 12;
          mPixels[idxB] = (uint8_t)b;
        }
      }

      // Background clear + phosphor render.
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
