#pragma once

// Visadhara Corona — 2D polar oscilloscope on the Mode ply's main
// fader area. Phase 3a (this commit): bare polar waveform reading
// the decimated audio buffer from Visadhara::Internal::vizBuf with
// Helicase-style smoothing infrastructure (snapshot caching, per-
// point slew, DC blocker, Catmull-Rom subdivision). No background,
// no rings, no trail yet — those land in 3b/3c/3d.
//
// Smoothing pipeline matches HelicaseOrbitalGraphic exactly:
//   - 256-sample snapshot from the viz ring buffer, refreshed every
//     2 draw frames (rather than every frame — reduces flicker).
//   - DC blocker: slow LP filter on the snapshot mean recenters the
//     wave so it sits at the base radius rather than drifting.
//   - Downsample 256 → 128 plot points via per-bin averaging.
//   - Per-point slew with α=0.08 LP factor (~12-frame time constant
//     at 30 fps draw rate) — gives the slow flowing motion that
//     reads as "smooth and considered" rather than rushed.
//   - 3× Catmull-Rom subdivision per base point → 384 effective
//     line segments around the circle. Curved polyline instead of
//     straight-segment angular shape.
//
// Header-only per feedback_no_out_of_line_virtuals (no .cpp file).
// 64-entry cos/sin LUT at file scope avoids runtime sinf/cosf per
// feedback_package_trig_lut; sub-point angles use linear LUT
// interpolation between adjacent integer indices.

#include <od/graphics/Graphic.h>
#include "Visadhara.h"
#include <string.h>

namespace stolmine
{
  // cos(2π · i / 64) for i = 0..63. Angle = i × 5.625°.
  // Step 0 at angle 0 (3 o'clock); increases counterclockwise.
  // Sub-point angles via linear interp between adjacent entries.
  static const float kCoronaCos[64] = {
    +1.00000000f, +0.99518473f, +0.98078528f, +0.95694034f, +0.92387953f,
    +0.88192126f, +0.83146961f, +0.77301045f, +0.70710678f, +0.63439328f,
    +0.55557023f, +0.47139674f, +0.38268343f, +0.29028468f, +0.19509032f,
    +0.09801714f, +0.00000000f, -0.09801714f, -0.19509032f, -0.29028468f,
    -0.38268343f, -0.47139674f, -0.55557023f, -0.63439328f, -0.70710678f,
    -0.77301045f, -0.83146961f, -0.88192126f, -0.92387953f, -0.95694034f,
    -0.98078528f, -0.99518473f, -1.00000000f, -0.99518473f, -0.98078528f,
    -0.95694034f, -0.92387953f, -0.88192126f, -0.83146961f, -0.77301045f,
    -0.70710678f, -0.63439328f, -0.55557023f, -0.47139674f, -0.38268343f,
    -0.29028468f, -0.19509032f, -0.09801714f, +0.00000000f, +0.09801714f,
    +0.19509032f, +0.29028468f, +0.38268343f, +0.47139674f, +0.55557023f,
    +0.63439328f, +0.70710678f, +0.77301045f, +0.83146961f, +0.88192126f,
    +0.92387953f, +0.95694034f, +0.98078528f, +0.99518473f
  };

  // sin(2π · i / 64) for i = 0..63.
  static const float kCoronaSin[64] = {
    +0.00000000f, +0.09801714f, +0.19509032f, +0.29028468f, +0.38268343f,
    +0.47139674f, +0.55557023f, +0.63439328f, +0.70710678f, +0.77301045f,
    +0.83146961f, +0.88192126f, +0.92387953f, +0.95694034f, +0.98078528f,
    +0.99518473f, +1.00000000f, +0.99518473f, +0.98078528f, +0.95694034f,
    +0.92387953f, +0.88192126f, +0.83146961f, +0.77301045f, +0.70710678f,
    +0.63439328f, +0.55557023f, +0.47139674f, +0.38268343f, +0.29028468f,
    +0.19509032f, +0.09801714f, +0.00000000f, -0.09801714f, -0.19509032f,
    -0.29028468f, -0.38268343f, -0.47139674f, -0.55557023f, -0.63439328f,
    -0.70710678f, -0.77301045f, -0.83146961f, -0.88192126f, -0.92387953f,
    -0.95694034f, -0.98078528f, -0.99518473f, -1.00000000f, -0.99518473f,
    -0.98078528f, -0.95694034f, -0.92387953f, -0.88192126f, -0.83146961f,
    -0.77301045f, -0.70710678f, -0.63439328f, -0.55557023f, -0.47139674f,
    -0.38268343f, -0.29028468f, -0.19509032f, -0.09801714f
  };

  class VisadharaCoronaGraphic : public od::Graphic
  {
  public:
    VisadharaCoronaGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpVisadhara(0) {}

    virtual ~VisadharaCoronaGraphic()
    {
      if (mpVisadhara)
        mpVisadhara->release();
    }

    void follow(Visadhara *p)
    {
      if (mpVisadhara)
        mpVisadhara->release();
      mpVisadhara = p;
      if (mpVisadhara)
        mpVisadhara->attach();
    }

  private:
    Visadhara *mpVisadhara;
    bool mSlewInit = false;
    int mUpdateCounter = 0;
    float mSnapshot[256];
    float mSlewShape[128];
    float mDcState = 0.0f;

    static const int kPoints = 128;   // base plot points around circle
    static const int kSubdiv = 3;     // Catmull-Rom subdivisions per segment

    // Catmull-Rom interpolation between 4 control points. Same form
    // as HelicaseOrbitalGraphic. tau=0.5 = standard tension.
    static inline float catmullRom(float p0, float p1, float p2, float p3,
                                   float t, float tau)
    {
      float t2 = t * t;
      float t3 = t2 * t;
      float a = -tau * p0 + (2.0f - tau) * p1 + (tau - 2.0f) * p2 + tau * p3;
      float b = 2.0f * tau * p0 + (tau - 3.0f) * p1 + (3.0f - 2.0f * tau) * p2 - tau * p3;
      float c = -tau * p0 + tau * p2;
      float d = p1;
      return a * t3 + b * t2 + c * t + d;
    }

  public:
#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      od::Graphic::draw(fb);

      const int w = mWidth;
      const int h = mHeight;
      const int left = mWorldLeft;
      const int bot = mWorldBottom;

      // Black plate baseline (Phase 3b will replace with background
      // gradient driven by Mode + Fold).
      fb.fill(BLACK, left, bot, left + w - 1, bot + h - 1);

      if (!mpVisadhara)
        return;

      if (!mSlewInit)
      {
        for (int i = 0; i < kPoints; i++)
          mSlewShape[i] = 0.0f;
        memset(mSnapshot, 0, sizeof(mSnapshot));
        mSlewInit = true;
      }

      // Snapshot the viz ring buffer every 2 frames. Halves CPU
      // and reduces flicker when audio is changing fast.
      mUpdateCounter++;
      if (mUpdateCounter >= 2)
      {
        mUpdateCounter = 0;
        for (int i = 0; i < 256; i++)
          mSnapshot[i] = mpVisadhara->getVizSample(i);
      }

      // DC blocker on snapshot: slow LP on the mean. Keeps the
      // polar plot centered on the base radius even if audio
      // accumulates DC offset from asymmetric folding.
      float dcSum = 0.0f;
      for (int i = 0; i < 256; i++)
        dcSum += mSnapshot[i];
      const float dcTarget = dcSum * (1.0f / 256.0f);
      mDcState += (dcTarget - mDcState) * 0.1f;

      // Downsample 256 → kPoints with per-bin averaging, DC
      // subtracted; then per-point slew (α=0.20, ~5-frame time
      // constant at 30fps). Less aggressive than the .20 build's
      // 0.08 so kick transients show without being instant-snap.
      for (int i = 0; i < kPoints; i++)
      {
        const int s0 = (i * 256) / kPoints;
        int s1 = ((i + 1) * 256) / kPoints;
        if (s1 > 256) s1 = 256;
        float avg = 0.0f;
        int count = s1 - s0;
        if (count < 1) count = 1;
        for (int j = s0; j < s1; j++)
          avg += mSnapshot[j] - mDcState;
        avg /= (float)count;
        mSlewShape[i] += (avg - mSlewShape[i]) * 0.20f;
      }

      // Geometric center + radius constants. Tightened baseR
      // (smaller resting circle) leaves more headroom for the
      // amplitude swing; bumped ampScale gives ~1.6× more radial
      // motion per audio unit. Max radius at amp=1 ≈ 0.50 of
      // minDim — wave reaches the graphic edge at full amplitude.
      const int cx = left + w / 2;
      const int cy = bot + h / 2;
      const int minDim = (w < h) ? w : h;
      const float baseR = (float)minDim * 0.28f;
      const float ampScale = (float)minDim * 0.22f;

      // Plot with 3× Catmull-Rom subdivision per base segment.
      // 128 × 3 = 384 line segments around the circle — smooth
      // curved polyline rather than angular straight segments.
      int firstX = 0, firstY = 0;
      int prevX = 0, prevY = 0;
      const int total = kPoints * kSubdiv;

      for (int i = 0; i < total; i++)
      {
        const int baseIdx = i / kSubdiv;
        const float subFrac = (float)(i % kSubdiv) / (float)kSubdiv;

        // Catmull-Rom across 4 neighboring slewed values.
        const int i0 = (baseIdx - 1 + kPoints) % kPoints;
        const int i1 = baseIdx % kPoints;
        const int i2 = (baseIdx + 1) % kPoints;
        const int i3 = (baseIdx + 2) % kPoints;
        const float val = catmullRom(mSlewShape[i0], mSlewShape[i1],
                                     mSlewShape[i2], mSlewShape[i3],
                                     subFrac, 0.5f);

        // Sub-point angle via linear interp between adjacent LUT
        // entries. Cheap; error at 5.625° spacing is negligible
        // (< 0.1% over a quarter cycle).
        const int lutStep = (kPoints / 64);   // base points per LUT entry = 2
        const float angIdx = (float)baseIdx / (float)lutStep + subFrac / (float)lutStep;
        const int li = (int)angIdx;
        const int lia = li & 63;
        const int lib = (li + 1) & 63;
        const float lfrac = angIdx - (float)li;
        const float ca = kCoronaCos[lia] + (kCoronaCos[lib] - kCoronaCos[lia]) * lfrac;
        const float sa = kCoronaSin[lia] + (kCoronaSin[lib] - kCoronaSin[lia]) * lfrac;

        const float r = baseR + val * ampScale;
        const int x = cx + (int)(ca * r);
        const int y = cy + (int)(sa * r);

        if (i == 0)
        {
          firstX = x;
          firstY = y;
        }
        else
        {
          if (x >= left && x < left + w && y >= bot && y < bot + h &&
              prevX >= left && prevX < left + w && prevY >= bot && prevY < bot + h)
          {
            fb.line(WHITE, prevX, prevY, x, y);
          }
        }
        prevX = x;
        prevY = y;
      }

      // Close the loop (final subdiv segment → first point).
      if (firstX >= left && firstX < left + w && firstY >= bot && firstY < bot + h &&
          prevX >= left && prevX < left + w && prevY >= bot && prevY < bot + h)
      {
        fb.line(WHITE, prevX, prevY, firstX, firstY);
      }
    }
#endif
  };
} // namespace stolmine
