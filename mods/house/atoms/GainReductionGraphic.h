// house::GainReductionGraphic
//
// Gain-reduction meter for a single 42x64 ply: a VU-style arc laid on
// its SIDE, so the needle sweeps top-to-bottom rather than left-to-
// right. A conventional VU dial is wider than it is tall and simply
// does not fit a ply; rotating it puts the long axis of the sweep along
// the long axis of the space.
//
//   0 dB of reduction  -> needle at the TOP
//   more reduction     -> needle swings DOWN
// which matches the physical intuition of a meter being pulled down by
// compression, and matches the direction of every gain-reduction meter
// on real hardware.
//
// NO RUNTIME TRIG. feedback_package_trig_lut records that
// single-precision sinf/cosf called from a package .so miscompute on
// am335x. The arc is a small compile-time table of needle endpoints,
// which is also far cheaper than trig in a draw path.
//
// The draw path runs on the `busy` task during insert and delete, which
// has a 4096-byte stack (feedback_draw_path_busy_stack), so there are
// no large locals here.
//
// od::Graphic subclasses stay HEADER-ONLY per
// feedback_no_out_of_line_virtuals.
#pragma once

#include <od/graphics/Graphic.h>
#include "GlueCompUnit.h"

namespace house
{

  // NAMESPACE SCOPE, not static class members. A `static constexpr`
  // member array is ODR-USED the moment it is indexed at runtime, and in
  // C++11 that needs an out-of-line definition the header cannot give -
  // so the package .so built fine and then failed to load with
  // "undefined symbol: house::GainReductionGraphic::kArcX", taking every
  // unit in the library down with it, not just this one.
  static const int kGrSteps = 24;
  static const float kGrMaxDb = 20.0f;

  // Quarter-arc from straight-up to straight-down-left, sampled at
  // compile time. Computed as cos/sin over [0, pi] * 0.5 so the sweep
  // reads as a dial rather than a bar.
  static const float kGrArcX[kGrSteps] = {
    0.000f, 0.136f, 0.269f, 0.396f, 0.516f, 0.626f, 0.724f, 0.809f,
    0.879f, 0.933f, 0.970f, 0.991f, 0.995f, 0.982f, 0.952f, 0.906f,
    0.844f, 0.768f, 0.679f, 0.578f, 0.468f, 0.350f, 0.227f, 0.100f
  };

  static const float kGrArcY[kGrSteps] = {
    1.000f, 0.991f, 0.963f, 0.918f, 0.857f, 0.780f, 0.690f, 0.588f,
    0.477f, 0.359f, 0.237f, 0.113f, -0.011f, -0.136f, -0.257f, -0.374f,
    -0.484f, -0.586f, -0.678f, -0.759f, -0.828f, -0.884f, -0.926f, -0.955f
  };

  class GainReductionGraphic : public od::Graphic
  {
  public:
    GainReductionGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~GainReductionGraphic()
    {
      if (mpComp) mpComp->release();
    }

    // OUTSIDE the SWIGLUA guard, deliberately. Everything inside that
    // guard is invisible to SWIG, so a follow() declared in there
    // compiles and links perfectly and is then nil in Lua - the unit
    // fails at CONSTRUCTION with "attempt to call a nil value". Every
    // other graphic in the tree puts follow() after the #endif.
    void follow(Gesso *comp)
    {
      if (mpComp) mpComp->release();
      mpComp = comp;
      if (mpComp) mpComp->attach();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      od::Graphic::draw(fb);

      const int cx = mWorldLeft + mWidth / 2;
      const int cy = mWorldBottom + mHeight - 6;   // pivot near the top
      const int R = mHeight - 18;

      // Arc ticks. kArc holds unit-circle offsets for the sweep,
      // precomputed rather than trig-per-frame.
      for (int i = 0; i < kGrSteps; i++)
      {
        const int x = cx + (int)(kGrArcX[i] * R);
        const int y = cy - (int)(kGrArcY[i] * R);
        // Every third tick is brighter, as a dial's labelled marks are.
        fb.pixel((i % 3 == 0) ? WHITE : GRAY7, x, y);
      }

      if (!mpComp) return;

      // Ballistics. The needle is SMOOTHED toward the reading rather
      // than following it sample-accurately: an unsmoothed needle on a
      // fast compressor is a blur and reads as noise, not as level.
      const float grDb = mpComp->gainReductionDb();
      const float target = grDb > kGrMaxDb ? kGrMaxDb : grDb;
      // Fast attack, slow fall - the same asymmetry a real meter has,
      // so peaks register and the eye can follow the recovery.
      if (target > mNeedle) mNeedle += (target - mNeedle) * 0.45f;
      else mNeedle += (target - mNeedle) * 0.08f;

      const float t = mNeedle / kGrMaxDb;          // 0..1 down the sweep
      int idx = (int)(t * (kGrSteps - 1) + 0.5f);
      if (idx < 0) idx = 0; else if (idx >= kGrSteps) idx = kGrSteps - 1;

      const int nx = cx + (int)(kGrArcX[idx] * R);
      const int ny = cy - (int)(kGrArcY[idx] * R);
      fb.line(WHITE, cx, cy, nx, ny);
      fb.circle(WHITE, cx, cy, 2);

      // Numeric readout, because a needle alone cannot be read exactly
      // and gain reduction is a number people want.
      char buf[8];
      if (mNeedle < 0.05f) snprintf(buf, sizeof buf, "0");
      else snprintf(buf, sizeof buf, "%.1f", mNeedle);
      fb.text(WHITE, mWorldLeft + 3, mWorldBottom + 2, buf, 10);
    }
#endif

  private:
    Gesso *mpComp = 0;
    float mNeedle = 0.0f;

  };

} // namespace house
