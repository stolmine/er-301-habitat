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

  // Radius of a regular K-gon (integer K) at angle theta, morphing
  // toward a K-pointed star as `morph` goes 0→1. At morph=0 the
  // return is the convex K-gon radius (apothem / cos(phi)). At
  // morph=1 the valleys (edge midpoints) are pulled in to
  // 0.38·rKgon while the points (vertices) stay at full radius.
  static inline float coronaKgonStar(float theta, int K, float morph, float rOuter)
  {
    const float twoPi = 6.28318530718f;
    const float sectorW = twoPi / (float)K;
    // local angle within one sector [0, sectorW); phi centered so
    // 0 = edge midpoint (star valley), ±sectorW/2 = vertex (point).
    const float local = theta - (float)((int)(theta / sectorW)) * sectorW;
    const float phi = local - sectorW * 0.5f;
    const float apothem = rOuter * lutCosRad(3.14159265f / (float)K);
    const float cphi = lutCosRad(phi);
    const float rKgon = (cphi > 0.0001f) ? (apothem / cphi) : rOuter;
    // Star modulation: 1.0 at the points, valleyRatio at the
    // valleys, linear between.
    const float halfSector = sectorW * 0.5f;
    float normPhi = (phi < 0.0f ? -phi : phi) / halfSector;  // 0 valley .. 1 point
    if (normPhi > 1.0f) normPhi = 1.0f;
    const float valleyRatio = 1.0f - morph * 0.62f;          // 1.0 .. 0.38
    const float starFactor = valleyRatio + (1.0f - valleyRatio) * normPhi;
    return rKgon * starFactor;
  }

  // Continuous-K radial function. Kf is a FLOAT so Mode changes are
  // smooth — a "4.5-gon" isn't a real closed shape, so we render
  // the radius as a crossfade between the floor(Kf)-gon and the
  // ceil(Kf)-gon radius at each fixed angular sample. Combined with
  // a fixed high vertex count this gives a shape that morphs
  // smoothly through the integer polygon counts instead of
  // popping.
  static inline float coronaRadius(float theta, float Kf, float morph, float rOuter)
  {
    int Klo = (int)Kf;
    if (Klo < 3) Klo = 3;
    const int Khi = Klo + 1;
    const float kfrac = Kf - (float)Klo;
    const float rLo = coronaKgonStar(theta, Klo, morph, rOuter);
    const float rHi = coronaKgonStar(theta, Khi, morph, rOuter);
    return rLo + (rHi - rLo) * kfrac;
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
      // Parameter mappings (Phase 3b + 3c):
      //   Spread   → N   : petal count, 1..8 (integer — petals are
      //              discrete by nature, no smooth interp needed).
      //   Mode     → Kf  : polygon sides as a FLOAT, 3.0..8.0.
      //              Rendered via the continuous coronaRadius()
      //              radial function (crossfade between adjacent
      //              integer polygons) so Mode sweeps morph the
      //              shape smoothly instead of popping side counts.
      //   Harmonic → R_petal : orbital radius. At H=0 petals cluster
      //              near center; at H=1 they spread to a wide orbit.
      //              Mirrors the audio voice distribution.
      //   Morph    → star: K-gon ↔ K-pointed star, also continuous
      //              via coronaRadius()'s star modulation.
      // All read live every frame — CV modulation animates the
      // geometry. Falls back to N=4 / Kf=6 / mid radius / convex if
      // no Visadhara is followed.
      const int minDim = (w < h) ? w : h;
      int N = 4;
      float Kf = 6.0f;
      float harmonicPos = 0.5f;
      float morphPos = 0.0f;
      if (mpVisadhara)
      {
        const float spreadPos = mpVisadhara->mSpread.value();
        const float modePos   = mpVisadhara->mMode.value();
        harmonicPos           = mpVisadhara->mHarmonic.value();
        morphPos              = mpVisadhara->mMorph.value();
        N = 1 + (int)(spreadPos * 7.0f);
        if (N < 1) N = 1;
        if (N > 8) N = 8;
        // Kf stays float: 3.0 (Skin) → 5.5 (Liquid) → 8.0 (Metal).
        Kf = 3.0f + modePos * 2.5f;
        if (Kf < 3.0f) Kf = 3.0f;
        if (Kf > 8.0f) Kf = 8.0f;
        if (harmonicPos < 0.0f) harmonicPos = 0.0f;
        if (harmonicPos > 1.0f) harmonicPos = 1.0f;
        if (morphPos < 0.0f) morphPos = 0.0f;
        if (morphPos > 1.0f) morphPos = 1.0f;
      }

      const float tiltAngle = 0.30f;     // ~17° elevated view
      // Harmonic drives the orbital radius. Minimum (H=0) sits at
      // 0.177 of minDim — where H=0.26 sat in the prior 0.12+0.22h
      // mapping — so the carousel never collapses too tight. Max
      // (H=1) stays at 0.34.
      const float R_petal = (float)minDim * (0.177f + 0.163f * harmonicPos);
      const float r_polygon = (float)minDim * 0.23f;   // petal size

      // Fixed-count vertex sampling for the continuous radial
      // function. 48 samples per petal — enough to render a K=8
      // star (16 features) crisply while morphing smoothly through
      // fractional K. Higher than the old 2K count but the radial
      // function needs dense sampling to capture star points.
      const int numVerts = 48;
      const float rOuter = r_polygon;

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

        int sx[48], sy[48];      // screen-projected x, y (numVerts == 48)
        float sz[48];            // depth (post-tilt z)

        for (int j = 0; j < numVerts; j++)
        {
          // Continuous K-gon/star vertex in local XY plane (vertical
          // face). Radius from coronaRadius() — smooth in both K
          // (Mode) and star morph (Morph).
          const float vAngle = 6.28318530718f * (float)j / (float)numVerts;
          const float radius = coronaRadius(vAngle, Kf, morphPos, rOuter);
          const float lx = radius * lutCosRad(vAngle);
          const float ly = radius * lutSinRad(vAngle);

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

        // Draw numVerts edges with per-line brightness from midpoint
        // depth. The "front edge" of each spinning shape (closer to
        // viewer) is bright; the "back edge" (farther) is dim. Cue
        // rotates with the spin.
        for (int j = 0; j < numVerts; j++)
        {
          const int nj = (j + 1) % numVerts;
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
