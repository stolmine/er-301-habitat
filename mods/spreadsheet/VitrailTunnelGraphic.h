#pragma once

#include <od/graphics/Graphic.h>
#include <Vitrail.h>
// Reuses the proven 72-entry cos/sin LUT + interpolating lutCos/lutSin helpers
// that Tomograph's response graphic already ships. Runtime sinf/cosf MISCOMPUTE
// when called from a package .so on am335x with the TI 4.9.3 toolchain
// (feedback_package_trig_lut), so every angle in here goes through that table.
// Include-guarded and internal-linkage, so sharing it costs nothing.
#include <FilterResponseGraphic.h>

namespace stolmine
{

  // Tunnel geometry constants. At namespace scope with internal linkage (the
  // FilterResponseGraphic kLutCos pattern) rather than in-class constexpr:
  // the build is -std=gnu++11, where an odr-used static constexpr array member
  // still needs an out-of-class definition.
  static const int kVtVerts = 24;
  static const int kVtRings = 10;
  static const int kVtStages = 6;
  static const float kVtVertStep = 2.0f * M_PI / (float)kVtVerts;
  static const float kVtInvRings = 1.0f / (float)kVtRings;
  static const float kVtFocal = 0.55f;    // ring size at unit depth
  static const float kVtBendAmt = 0.30f;  // lateral swing of the far end
  static const float kVtLean = 0.55f;     // SIGNED shear -> leans left or right
  static const float kVtSquash = 0.30f;   // foreshortening as it banks
  static const float kVtTwist = 0.40f;    // radians of spiral per unit depth
  static const float kVtWobble = 0.17f;   // per-ring out-of-phase wander
  static const float kVtWobFreq = 1.70f;  // deliberately not a ring multiple
  static const float kVtSwayAmt = 0.06f;  // vertical breathing of the tube
  static const float kVtSwayPitch = 0.45f;

  // Edge count steps DOWN through discrete regular polygons as resonance
  // rises - circle, 12-gon, octagon, hexagon, square, triangle - rather than
  // morphing continuously through rounded intermediates. Every one of these
  // divides 24, so their corners land exactly on vertex samples and stay
  // sharp at any rotation. A pentagon does not divide 24, which is why the
  // ladder skips 5.
  //
  // Each profile is the polygon's radius sampled at the 24 vertex angles,
  // normalised so its mean radius is 1: the tunnel keeps a steady visual
  // weight as it steps down instead of shrinking toward the smaller areas.
  static const float kVtProfile[kVtStages][kVtVerts] = {
      // circle
      {+1.000000f, +1.000000f, +1.000000f, +1.000000f, +1.000000f, +1.000000f,
       +1.000000f, +1.000000f, +1.000000f, +1.000000f, +1.000000f, +1.000000f,
       +1.000000f, +1.000000f, +1.000000f, +1.000000f, +1.000000f, +1.000000f,
       +1.000000f, +1.000000f, +1.000000f, +1.000000f, +1.000000f, +1.000000f},
      // 12-gon
      {+1.017332f, +0.982668f, +1.017332f, +0.982668f, +1.017332f, +0.982668f,
       +1.017332f, +0.982668f, +1.017332f, +0.982668f, +1.017332f, +0.982668f,
       +1.017332f, +0.982668f, +1.017332f, +0.982668f, +1.017332f, +0.982668f,
       +1.017332f, +0.982668f, +1.017332f, +0.982668f, +1.017332f, +0.982668f},
      // octagon
      {+1.047595f, +0.976203f, +0.976203f, +1.047595f, +0.976203f, +0.976203f,
       +1.047595f, +0.976203f, +0.976203f, +1.047595f, +0.976203f, +0.976203f,
       +1.047595f, +0.976203f, +0.976203f, +1.047595f, +0.976203f, +0.976203f,
       +1.047595f, +0.976203f, +0.976203f, +1.047595f, +0.976203f, +0.976203f},
      // hexagon
      {+1.093142f, +0.980084f, +0.946689f, +0.980084f, +1.093142f, +0.980084f,
       +0.946689f, +0.980084f, +1.093142f, +0.980084f, +0.946689f, +0.980084f,
       +1.093142f, +0.980084f, +0.946689f, +0.980084f, +1.093142f, +0.980084f,
       +0.946689f, +0.980084f, +1.093142f, +0.980084f, +0.946689f, +0.980084f},
      // square
      {+1.248907f, +1.019728f, +0.914263f, +0.883110f, +0.914263f, +1.019728f,
       +1.248907f, +1.019728f, +0.914263f, +0.883110f, +0.914263f, +1.019728f,
       +1.248907f, +1.019728f, +0.914263f, +0.883110f, +0.914263f, +1.019728f,
       +1.248907f, +1.019728f, +0.914263f, +0.883110f, +0.914263f, +1.019728f},
      // triangle
      {+1.567340f, +1.108277f, +0.904904f, +0.811315f, +0.783670f, +0.811315f,
       +0.904904f, +1.108277f, +1.567340f, +1.108277f, +0.904904f, +0.811315f,
       +0.783670f, +0.811315f, +0.904904f, +1.108277f, +1.567340f, +1.108277f,
       +0.904904f, +0.811315f, +0.783670f, +0.811315f, +0.904904f, +1.108277f}};

  // VitrailTunnelGraphic - the view down a switched-capacitor tunnel.
  //
  // Not a 3D scene: a stack of 2D polygons scaled by 1/depth and drawn far to
  // near, which is the classic demoscene tunnel and produces an identical
  // picture for a fraction of the cost. Travelling forward is just decrementing
  // depth and recycling the ring that flies past the viewer.
  //
  // Everything it draws is driven by the DSP's baked per-block viz scalars, so
  // the animation clock lives in the audio thread and tracks the real
  // switched-cap clocks rather than free-running off the UI frame rate:
  //   travel     -> forward motion      (mean cutoff = the SC clock rate)
  //   spin       -> mouth rotation      (the A/B clock drift beat)
  //   angularity -> circle to triangle  (resonance)
  //   bend       -> curve + bank        (A/B cutoff imbalance)
  //   level      -> brightness pulse    (output envelope)
  //
  // Header-only with every virtual defined inline: an out-of-line virtual on an
  // od::* subclass triggers the GCC key-function rule and hard-faults on insert
  // (feedback_no_out_of_line_virtuals, tools/check-graphic-virtual-defs.sh).
  class VitrailTunnelGraphic : public od::Graphic
  {
  public:
    VitrailTunnelGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height)
    {
      for (int i = 0; i < kVtVerts; i++)
      {
        mPx[i] = 0.0f;
        mPy[i] = 0.0f;
      }
    }

    virtual ~VitrailTunnelGraphic()
    {
      if (mpVitrail)
        mpVitrail->release();
    }

    void follow(Vitrail *pVitrail)
    {
      if (mpVitrail)
        mpVitrail->release();
      mpVitrail = pVitrail;
      if (mpVitrail)
        mpVitrail->attach();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      od::Graphic::draw(fb);
      if (!mpVitrail)
        return;

      const int w = mWidth, h = mHeight;
      const int left = mWorldLeft, bot = mWorldBottom;
      const int cx0 = left + w / 2;
      const int cy0 = bot + h / 2;

      const float travel = mpVitrail->getTravel();
      const float spin = mpVitrail->getSpin();
      const float ang = mpVitrail->getAngularity();
      const float bend = mpVitrail->getBend();
      const float level = mpVitrail->getLevel();
      const float spinRad = spin * 2.0f * M_PI;

      // ---- unit shape, computed ONCE per frame -------------------------
      // Resonance selects a DISCRETE polygon from the ladder; it snaps from
      // one edge count to the next instead of morphing through rounded
      // in-between shapes.
      int stage = (int)(ang * (float)kVtStages);
      if (stage < 0)
        stage = 0;
      if (stage > kVtStages - 1)
        stage = kVtStages - 1;
      const float *prof = kVtProfile[stage];

      // Unrotated unit shape. The 24 vertex angles are fixed multiples of
      // 2pi/24, which is exactly stride 3 through the 72-entry LUT, so these
      // are DIRECT table reads with no interpolation at all. Per-ring rotation
      // is then a 2x2 rotate of these shared points, which costs far less than
      // re-sampling 24 interpolated angles for every ring.
      for (int i = 0; i < kVtVerts; i++)
      {
        mPx[i] = prof[i] * kLutCos[i * 3];
        mPy[i] = prof[i] * kLutSin[i * 3];
      }

      // Half-extent the nearest visible ring is allowed to reach. Generous, so
      // rings genuinely fly past the viewer; the clipper keeps them in the ply.
      const float span = (float)(w < h ? w : h) * 0.5f;

      // Signed shear. A symmetric squash reads as the same lean whichever way
      // the imbalance goes, so the direction has to live in a term that keeps
      // its sign; this one leans left for negative bend and right for positive.
      const float lean = bend * kVtLean;
      const float squash = 1.0f - kVtSquash * (bend < 0.0f ? -bend : bend);

      // ---- rings, far to near ------------------------------------------
      for (int ring = kVtRings - 1; ring >= 0; ring--)
      {
        // depth in (0, kVtRings]: as travel runs 0->1 every ring advances one
        // slot toward the viewer, and at the wrap the set is identical with a
        // fresh ring appearing at the far end. Seamless recycling.
        float d = (float)ring + 1.0f - travel;
        if (d < 0.06f)
          continue; // essentially past the camera

        float scale = kVtFocal / d;
        float rx = span * scale;
        if (rx > span * 6.0f)
          continue; // so far past the viewer nothing of it lands in the ply

        // Per-ring rotation. Every term is a function of DEPTH, never of the
        // array index: rings recycle, so an index-keyed angle would snap when
        // a ring wrapped. The linear twist is the spiral (each ring further
        // down the tube is turned a little further, so the whole tube reads as
        // one continuous helix), and the wobble at a non-ring-multiple rate
        // keeps neighbours from sitting in a rigid lockstep progression.
        float ringAng = spinRad + d * kVtTwist +
                        kVtWobble * lutSin(d * kVtWobFreq + spinRad * 0.5f);
        float ca = lutCos(ringAng);
        float sa = lutSin(ringAng);

        float depthFrac = d * kVtInvRings;
        float ry = rx * squash;
        float cx = (float)cx0 + bend * kVtBendAmt * (float)w * depthFrac;
        // Gentle sway down the length of the tube so the tunnel breathes
        // rather than sitting rigid; this is the "curving torus" term.
        float cy = (float)cy0 + kVtSwayAmt * (float)h * lutSin(spinRad * 0.5f + d * kVtSwayPitch);

        // Depth cue across the 16 grey levels: near rings bright, far rings
        // sink toward the background, modulated by the output envelope so the
        // tunnel pulses with what you hear.
        float bright = (1.0f - depthFrac * 0.85f) * (0.35f + 0.65f * level);
        int shade = (int)(bright * 15.0f + 0.5f);
        if (shade < 1)
          shade = 1;
        if (shade > 15)
          shade = 15;

        int px = 0, py = 0;
        for (int i = 0; i <= kVtVerts; i++)
        {
          int k = (i == kVtVerts) ? 0 : i;
          // rotate the shared unit point, then shear, then project
          float xr = mPx[k] * ca - mPy[k] * sa;
          float yr = mPx[k] * sa + mPy[k] * ca;
          xr += lean * yr;
          int qx = (int)(cx + xr * rx + 0.5f);
          int qy = (int)(cy + yr * ry + 0.5f);
          if (i > 0)
            clippedLine(fb, (od::Color)shade, px, py, qx, qy);
          px = qx;
          py = qy;
        }
      }
    }
#endif

  private:
    Vitrail *mpVitrail = nullptr;

    // Class members, never stack locals: auto-vectorised stack arrays emit
    // NEON loads with :64 alignment hints that trap on Cortex-A8
    // (feedback_neon_intrinsics_drumvoice).
    float mPx[kVtVerts];
    float mPy[kVtVerts];

    // od's line() only guards the SCREEN edge, so a near ring would happily
    // scribble across the neighbouring plies. Cohen-Sutherland against this
    // graphic's own rect before anything is drawn.
    static int outcode(int x, int y, int l, int b, int r, int t)
    {
      int c = 0;
      if (x < l) c |= 1;
      else if (x > r) c |= 2;
      if (y < b) c |= 4;
      else if (y > t) c |= 8;
      return c;
    }

    void clippedLine(od::FrameBuffer &fb, od::Color color,
                     int x0, int y0, int x1, int y1)
    {
      const int l = mWorldLeft, b = mWorldBottom;
      const int r = l + mWidth - 1, t = b + mHeight - 1;
      int c0 = outcode(x0, y0, l, b, r, t);
      int c1 = outcode(x1, y1, l, b, r, t);

      for (int guard = 0; guard < 8; guard++)
      {
        if (!(c0 | c1))
        {
          fb.line(color, x0, y0, x1, y1);
          return;
        }
        if (c0 & c1)
          return; // wholly outside

        int c = c0 ? c0 : c1;
        int nx = 0, ny = 0;
        if (c & 8) { ny = t; nx = x0 + (x1 - x0) * (t - y0) / (y1 - y0); }
        else if (c & 4) { ny = b; nx = x0 + (x1 - x0) * (b - y0) / (y1 - y0); }
        else if (c & 2) { nx = r; ny = y0 + (y1 - y0) * (r - x0) / (x1 - x0); }
        else { nx = l; ny = y0 + (y1 - y0) * (l - x0) / (x1 - x0); }

        if (c == c0) { x0 = nx; y0 = ny; c0 = outcode(x0, y0, l, b, r, t); }
        else { x1 = nx; y1 = ny; c1 = outcode(x1, y1, l, b, r, t); }
      }
    }
  };

} // namespace stolmine
