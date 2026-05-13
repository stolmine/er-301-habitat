#pragma once

// Visadhara Corona — 2D polar oscilloscope on the Mode ply's main
// fader area. Phase 3a (this commit): bare polar waveform reading
// the decimated audio buffer from Visadhara::Internal::vizBuf. No
// background, no rings, no trail yet — those land in 3b/3c/3d.
//
// Header-only per feedback_no_out_of_line_virtuals (no .cpp file,
// vtable stays COMDAT). 64-entry cos/sin LUT at file scope avoids
// runtime sinf/cosf which miscompute from a package .so on am335x
// per feedback_package_trig_lut. The LUT has 64 entries lining up
// 1:1 with the 64-sample viz buffer — direct indexing, no
// interpolation, no floorf.

#include <od/graphics/Graphic.h>
#include "Visadhara.h"

namespace stolmine
{
  // cos(2π · i / 64) for i = 0..63. Angle = i × 5.625°.
  // Step 0 at angle 0 (3 o'clock); increases counterclockwise.
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

      // Geometric center of the graphic and a base radius. Audio
      // samples modulate the radius around the base, producing a
      // pulsing-ring polar plot. Base radius leaves room for ±1
      // amplitude excursion within the graphic bounds.
      const int cx = left + w / 2;
      const int cy = bot + h / 2;
      const int minDim = (w < h) ? w : h;
      const float baseR = (float)minDim * 0.34f;
      const float ampScale = (float)minDim * 0.14f;

      // Read all 64 viz samples, project to polar coords, draw a
      // closed polyline around the circle. Step i maps to age=i
      // (newest at angle 0, older clockwise around the circle).
      int firstX = 0, firstY = 0;
      int prevX = 0, prevY = 0;

      for (int i = 0; i < 64; i++)
      {
        float sample = mpVisadhara->getVizSample(i);
        if (sample > 1.0f) sample = 1.0f;
        else if (sample < -1.0f) sample = -1.0f;

        const float r = baseR + sample * ampScale;
        const int x = cx + (int)(kCoronaCos[i] * r);
        const int y = cy + (int)(kCoronaSin[i] * r);

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

      // Close the loop (sample 63 → sample 0).
      if (firstX >= left && firstX < left + w && firstY >= bot && firstY < bot + h &&
          prevX >= left && prevX < left + w && prevY >= bot && prevY < bot + h)
      {
        fb.line(WHITE, prevX, prevY, firstX, firstY);
      }
    }
#endif

  private:
    Visadhara *mpVisadhara;
  };
} // namespace stolmine
