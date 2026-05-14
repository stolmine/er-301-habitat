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

      // Advance both rotation axes. Speeds chosen for slow legible
      // motion at the 30 fps draw rate. mGlobalTumble at ~0.7°/frame
      // = ~21°/sec — petals complete an orbit in ~17 seconds.
      // mPetalSpin at ~1.4°/frame = ~43°/sec — each petal rotates
      // its own face in ~8.4 seconds. Distinct enough rates that
      // the two motions don't synchronize.
      mGlobalTumble += 0.012f;
      mPetalSpin += 0.025f;
      // Keep angles in a sane range to avoid float precision drift
      // over long runs. 1000 full rotations is plenty before wrap.
      if (mGlobalTumble > 6283.0f) mGlobalTumble -= 6283.0f;
      if (mPetalSpin > 6283.0f) mPetalSpin -= 6283.0f;

      // Geometry constants. Fixed for Phase 3a'; parameters take
      // these over in 3b+.
      const int N = 4;                  // petal instances
      const int K = 6;                  // sides per polygon (hexagon)
      const float tiltAngle = 0.50f;    // ~28.6° tilt around X axis
      const int minDim = (w < h) ? w : h;
      const float R_petal = (float)minDim * 0.30f;     // orbit radius
      const float r_polygon = (float)minDim * 0.13f;   // petal size

      const float cosTilt = lutCosRad(tiltAngle);
      const float sinTilt = lutSinRad(tiltAngle);

      const int cx = left + w / 2;
      const int cy = bot + h / 2;

      // Draw each petal: project orbital center to 3D-tilted screen
      // position; render K-gon at that position with its own spin.
      for (int i = 0; i < N; i++)
      {
        const float orbitAngle = mGlobalTumble + 6.28318530718f * (float)i / (float)N;
        const float cosOrbit = lutCosRad(orbitAngle);
        const float sinOrbit = lutSinRad(orbitAngle);

        // Orbit on the world XZ plane (Y=0 always). Tilting around
        // X by tiltAngle maps Z → screen Y and preserves X.
        //   worldX = R * cosOrbit
        //   worldZ = R * sinOrbit
        //   After tilt: y' = -worldZ * sinTilt
        //               z' =  worldZ * cosTilt
        // Z position (depth) determines brightness.
        const float petalScreenX = R_petal * cosOrbit;
        const float petalScreenY = -R_petal * sinOrbit * sinTilt;
        const float depthZ      =  R_petal * sinOrbit * cosTilt;

        // Brightness from depth: petals in front (depthZ > 0) brighter,
        // petals in back (depthZ < 0) dimmer. Range gray 4-14.
        const float depthN = 0.5f + 0.5f * (depthZ / R_petal);   // 0..1
        int petalBright = 4 + (int)(depthN * 10.0f);
        if (petalBright < 4) petalBright = 4;
        if (petalBright > 14) petalBright = 14;

        // Per-petal spin angle. Same speed for all petals but with
        // a per-index phase offset so they don't all show the same
        // orientation at once.
        const float spinAngle = mPetalSpin + 6.28318530718f * (float)i / (float)(N * 2);

        // Compute K vertex positions for this petal. Centered at
        // (petalScreenX, petalScreenY), radius r_polygon, rotated
        // by spinAngle.
        int vx[16], vy[16];  // K up to 16 reserved
        for (int j = 0; j < K; j++)
        {
          const float vAngle = spinAngle + 6.28318530718f * (float)j / (float)K;
          const float vox = r_polygon * lutCosRad(vAngle);
          const float voy = r_polygon * lutSinRad(vAngle);
          vx[j] = cx + (int)(petalScreenX + vox);
          vy[j] = cy + (int)(petalScreenY + voy);
        }

        // Draw closed polygon: K edges connecting consecutive vertices.
        // Bounds-check each line to silently clip outside the graphic.
        for (int j = 0; j < K; j++)
        {
          const int nj = (j + 1) % K;
          if (vx[j] >= left && vx[j] < left + w &&
              vy[j] >= bot  && vy[j] < bot + h  &&
              vx[nj] >= left && vx[nj] < left + w &&
              vy[nj] >= bot  && vy[nj] < bot + h)
          {
            fb.line(petalBright, vx[j], vy[j], vx[nj], vy[nj]);
          }
        }
      }
    }
#endif
  };
} // namespace stolmine
