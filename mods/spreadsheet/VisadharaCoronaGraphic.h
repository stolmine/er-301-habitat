#pragma once

// Visadhara Corona — spirograph/arabesque viz on the Mode ply's
// main fader area. Phase 3a' (this commit): scaffold the geometric
// engine with fixed values. N=4 instances of a K=6 hexagon, each
// orbiting around screen center on a tilted plane while
// independently spinning around its own axis. Parameter mappings
// (Spread → N, Mode → K, Harmonic → petal direction, Morph → star
// twist, Decay → trim sweep, Fold → contrast, V/Oct → tumble speed)
// land in subsequent phases 3b-3e.
//
// Geometry (three rotations):
//   1. Global tumble: whole assembly rotates around central Z axis
//      (orbital path). Continuous slow advance per frame.
//   2. 3D tilt: orbital plane is tilted ~28° around the X axis so
//      the orbit appears as a vertical ellipse rather than a flat
//      circle. Fixed angle for Phase 3a' (probably forever — small
//      display needs the depth cue for visibility).
//   3. Per-petal spin: each petal-polygon rotates around its own
//      center independently. Same angular velocity for all petals
//      with per-petal phase offset for visual variety.
//
// Depth shading: petal brightness varies with z-position
// (orbital depth). Petals in front (toward viewer) are brighter
// than petals in back. Same precedent as HelicaseOrbitalGraphic's
// depth shading.
//
// Header-only per feedback_no_out_of_line_virtuals.
//
// LUT trig per feedback_package_trig_lut: 64-entry cos/sin tables
// at file scope. lutCosRad / lutSinRad helpers do linear interp
// for arbitrary radians (FilterResponseGraphic pattern).

#include <od/graphics/Graphic.h>
#include "Visadhara.h"

namespace stolmine
{
  // cos(2π · i / 64) for i = 0..63. Angle = i × 5.625°.
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

  // Arbitrary-radian LUT lookup with linear interp between adjacent
  // 64-entry samples. Bias by a large multiple of 64 so negative
  // angles cast safely through (int) without floorf (also libm).
  static inline float lutCosRad(float a)
  {
    const float scale = 64.0f / 6.28318530718f;   // 64 / 2π
    const float biased = a * scale + 64.0f * 1000.0f;
    const int ii = (int)biased;
    const float frac = biased - (float)ii;
    const int i = ii & 63;
    const int next = (i + 1) & 63;
    return kCoronaCos[i] + (kCoronaCos[next] - kCoronaCos[i]) * frac;
  }

  static inline float lutSinRad(float a)
  {
    const float scale = 64.0f / 6.28318530718f;
    const float biased = a * scale + 64.0f * 1000.0f;
    const int ii = (int)biased;
    const float frac = biased - (float)ii;
    const int i = ii & 63;
    const int next = (i + 1) & 63;
    return kCoronaSin[i] + (kCoronaSin[next] - kCoronaSin[i]) * frac;
  }

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
    // Continuous animation state — both advance every draw frame.
    float mGlobalTumble = 0.0f;    // orbital rotation, Z axis
    float mPetalSpin = 0.0f;       // per-petal own-axis rotation

  public:
#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      od::Graphic::draw(fb);

      const int w = mWidth;
      const int h = mHeight;
      const int left = mWorldLeft;
      const int bot = mWorldBottom;

      fb.fill(BLACK, left, bot, left + w - 1, bot + h - 1);

      // Advance both rotation axes. Carousel orbit slower; petal
      // spin faster. Crucially the two RATES are different (and
      // not integer multiples) so configurations evolve continuously
      // — each frame shows a new relationship between orbital
      // position and petal orientation. Ratio 0.030 / 0.012 = 2.5
      // (rational but not aligned to N=4 symmetries).
      mGlobalTumble += 0.012f;   // carousel orbit, ~17 sec per turn
      mPetalSpin += 0.030f;      // petal vertical-axis spin, ~7 sec
      if (mGlobalTumble > 6283.0f) mGlobalTumble -= 6283.0f;
      if (mPetalSpin > 6283.0f) mPetalSpin -= 6283.0f;

      // Geometry: vertical-standing K-gons on a horizontal carousel.
      //   - K-gon local plane = world XY (vertical, faces camera at
      //     spin=0). Vertices at (lx, ly, 0) with lx, ly in K-gon
      //     local space.
      //   - Each petal spins around its own vertical axis (the world
      //     Y axis through its center): K-gon X axis rotates toward
      //     world Z. At spin=π/2, K-gon is edge-on (zero horizontal
      //     extent, full vertical extent).
      //   - Petal centers travel on a horizontal carousel (world XZ
      //     plane, Y=0).
      //   - Carousel tilted slightly around world X axis for an
      //     elevated view (mild 3D depth cue without consuming much
      //     vertical screen space).
      //   - All petals share the same spin angle (no per-petal phase
      //     offset). They all reach face-on / edge-on simultaneously
      //     but at different orbital positions. The rate difference
      //     between orbit and spin (5:2-ish) drives the parallax
      //     evolution.
      // Phase 3b parameter mappings:
      //   Spread → N  : petal count. 1 voice-cluster petal at
      //                 Spread=0 (audio voices all at fundamental)
      //                 up to 8 petals at Spread=1 (voices spread
      //                 across the harmonic/prime series). The
      //                 viz spreads as the audio spreads.
      //   Mode   → K  : polygon sides. 3 (triangle) at Skin,
      //                 ~5 (pentagon) at Liquid, 8 (octagon) at
      //                 Metal. More sides = more complex shape,
      //                 mirroring timbral complexity.
      // Both read live every frame — CV modulation of Mode/Spread
      // animates the geometry. Integer steps cause discrete petal
      // pop-in / side-count changes; honest representation of the
      // discrete-ish nature of those param ranges. Falls back to
      // N=4 / K=6 if no Visadhara is followed.
      int N = 4;
      int K = 6;
      if (mpVisadhara)
      {
        const float spreadPos = mpVisadhara->mSpread.value();
        const float modePos   = mpVisadhara->mMode.value();
        N = 1 + (int)(spreadPos * 7.0f);
        if (N < 1) N = 1;
        if (N > 8) N = 8;
        K = 3 + (int)(modePos * 2.5f);
        if (K < 3) K = 3;
        if (K > 8) K = 8;   // sx/sy/sz arrays sized 16, K=8 safe
      }
      const float tiltAngle = 0.30f;     // ~17° elevated view
      const int minDim = (w < h) ? w : h;
      const float R_petal = (float)minDim * 0.30f;     // orbit radius
      const float r_polygon = (float)minDim * 0.23f;   // petal size

      const float cosTilt = lutCosRad(tiltAngle);
      const float sinTilt = lutSinRad(tiltAngle);

      const int cx = left + w / 2;
      const int cy = bot + h / 2;

      // Shared spin (same for all petals).
      const float cosSpin = lutCosRad(mPetalSpin);
      const float sinSpin = lutSinRad(mPetalSpin);

      for (int i = 0; i < N; i++)
      {
        // Per-petal orbital position on the horizontal carousel.
        const float orbitAngle = mGlobalTumble + 6.28318530718f * (float)i / (float)N;
        const float petalWorldX = R_petal * lutCosRad(orbitAngle);
        const float petalWorldZ = R_petal * lutSinRad(orbitAngle);

        int sx[16], sy[16];      // screen-projected x, y
        float sz[16];            // depth (post-tilt z)

        for (int j = 0; j < K; j++)
        {
          // K-gon vertex in local XY plane (vertical face).
          const float vAngle = 6.28318530718f * (float)j / (float)K;
          const float lx = r_polygon * lutCosRad(vAngle);
          const float ly = r_polygon * lutSinRad(vAngle);

          // Spin around the petal's vertical axis (world Y axis
          // through the petal center). Local X rotates toward Z.
          //   localX'  =  lx · cosSpin
          //   localY'  =  ly
          //   localZ'  = -lx · sinSpin
          const float localX = lx * cosSpin;
          const float localY = ly;
          const float localZ = -lx * sinSpin;

          // Translate to petal center in world space.
          const float worldVx = petalWorldX + localX;
          const float worldVy = localY;
          const float worldVz = petalWorldZ + localZ;

          // Tilt the whole world around X axis for elevated view.
          const float tiltedY = worldVy * cosTilt - worldVz * sinTilt;
          const float tiltedZ = worldVy * sinTilt + worldVz * cosTilt;

          sx[j] = cx + (int)worldVx;
          sy[j] = cy + (int)tiltedY;
          sz[j] = tiltedZ;
        }

        // Draw K edges with per-line brightness from midpoint depth.
        // The "front edge" of each spinning K-gon (closer to viewer)
        // is bright; the "back edge" (farther) is dim. Cue rotates
        // with the spin.
        for (int j = 0; j < K; j++)
        {
          const int nj = (j + 1) % K;
          const float midZ = (sz[j] + sz[nj]) * 0.5f;
          const float maxAbsZ = (R_petal + r_polygon) * cosTilt;
          const float depthN = 0.5f + 0.5f * (midZ / maxAbsZ);
          int bright = 3 + (int)(depthN * 12.0f);
          if (bright < 3) bright = 3;
          if (bright > 15) bright = 15;

          if (sx[j] >= left && sx[j] < left + w &&
              sy[j] >= bot  && sy[j] < bot + h  &&
              sx[nj] >= left && sx[nj] < left + w &&
              sy[nj] >= bot  && sy[nj] < bot + h)
          {
            fb.line(bright, sx[j], sy[j], sx[nj], sy[nj]);
          }
        }
      }
    }
#endif
  };
} // namespace stolmine
