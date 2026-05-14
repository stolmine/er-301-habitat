#pragma once

// Visadhara Corona — spirograph/arabesque viz on the Mode ply's
// main fader area. Phase 3a' (this commit): scaffold the geometric
// engine with fixed values. N=4 instances of a K=6 hexagon, each
// orbiting around screen center on a tilted plane while
// independently spinning around its own axis. Parameter mappings:
// Spread → N (petal count), Mode → K (polygon sides), Harmonic →
// carousel radius, Morph → star points — all live (phases 3b/3c).
// Phase 3d adds trigger-driven radial shockwave bands that modulate
// per-pixel brightness, with Attack/Decay driving band kinematics.
// Fold → band polarity and V/Oct → tumble speed land in phase 3e.
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

#include <math.h>
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

  // Phase-3d shockwave band brightness gain. A band at full strength
  // (raised-cosine peak = 1.0) adds this many brightness levels to
  // the depth-shaded base — enough to flare a mid-gray wireframe
  // line to full white. Multiplied by the band polarity (+1 reveal /
  // -1 obscure) at the call site; overlapping bands sum, clamped
  // 0..15.
  static const float kCoronaBandStrength = 11.0f;

  // Band-position easing. A band travels its startPos→endPos span on
  // a quadratic ease-out curve (t·(2−t)) rather than linearly — a
  // fast initial burst that decelerates, the way a shockwave loses
  // energy as it expands. Single tunable point: swap for a cubic
  // (1−(1−t)³) or smoothstep here to restyle every band at once.
  static inline float coronaEase(float t)
  {
    return t * (2.0f - t);
  }

  // Phase-3e Fold contour field — nodule seed points. The background
  // at Fold>0 is a multi-source radial-wave interference field:
  // concentric rings emanate from each nodule, and rings from
  // neighbouring nodules merge/interfere — approximating a
  // reaction-diffusion / Turing-pattern look without a stateful,
  // multi-pass RD simulation. Positions are normalized [0,1]² within
  // the graphic's region, a hand-scattered set (deliberately off-grid
  // and off-edge). Static — a backdrop; only the ring phase animates.
  // Count is the dominant cost dial (per-pixel work is O(nodules)).
  static const int kCoronaNoduleCount = 6;
  static const float kCoronaNoduleX[kCoronaNoduleCount] = {
    0.20f, 0.68f, 0.42f, 0.85f, 0.30f, 0.74f
  };
  static const float kCoronaNoduleY[kCoronaNoduleCount] = {
    0.25f, 0.16f, 0.52f, 0.62f, 0.82f, 0.88f
  };
  // Ring spatial frequency — roughly one full cycle per ~7 px.
  static const float kCoronaRingFreq = 0.90f;

  class VisadharaCoronaGraphic : public od::Graphic
  {
  public:
    VisadharaCoronaGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpVisadhara(0) {}

    virtual ~VisadharaCoronaGraphic()
    {
      if (mpVisadhara)
        mpVisadhara->release();
      delete[] mFieldC;
      delete[] mFieldS;
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
    float mFoldPhase = 0.0f;       // Fold contour-field ring phase

    // Fold contour-field frame cache. The per-pixel ring sum
    // Σcos(dₙ·freq − phase) factors via the angle-subtraction identity
    // into phase-INDEPENDENT cos/sin tables —
    //   field[i] = cos(phase)·mFieldC[i] + sin(phase)·mFieldS[i]
    // so the sqrt/cos work happens ONCE in buildFoldCache (lazily, on
    // first draw, sized to the region) and the per-frame drawFoldField
    // is just a cheap MAC + threshold per pixel. ~2688 floats each.
    float *mFieldC = nullptr;
    float *mFieldS = nullptr;
    int mFieldCacheW = 0;
    int mFieldCacheH = 0;

    // --- Phase-3d trigger-driven radial shockwave bands ---
    // On each trigger Visadhara reports (vizTriggerCount advances)
    // the graphic emits two bands: an OUTWARD shockwave from the
    // center (Decay sets its lifetime — long decay = slow languid
    // sweep, short decay = fast snap) and an INWARD collapse from
    // beyond the rim (Attack sets its lifetime — slow attack = slow
    // visible converge, instant/negative attack = quick flash). Each
    // band modulates per-pixel brightness with a raised-cosine
    // profile so its edges gradate smoothly, and travels its span on
    // the coronaEase() curve rather than linearly. Several can be in
    // flight at once (overlapping pulses); emission is capped at one
    // pair per frame, so dense trigger streams thin to a framerate-
    // bound flicker / strobe — intentional.
    static const int kMaxBands = 8;
    struct Band
    {
      float t;          // lifetime progress, 0..1 (advances linearly)
      float tInc;       // per-frame progress increment
      float startPos;   // normalized radius at t=0
      float endPos;     // normalized radius at t=1
      float pos;        // cached eased position: lerp(start,end,ease(t))
      float halfWidth;  // raised-cosine half-extent, normalized radius
      bool  active;
    };
    Band mBands[kMaxBands] = {};
    int  mLastTriggerCount = -1;   // <0 → not yet synced to the unit
    int  mNextBandSlot = 0;        // ring index for band emission

    // Band polarity, refreshed every frame from Fold (Phase 3e):
    // +1 = reveal (bands brighten the wireframe), -1 = obscure
    // (bands darken it toward black), continuous through 0 at the
    // mid-Fold crossover. Multiplied into bandGain and applied as a
    // signed brightness delta with a 0..15 dual clamp in
    // drawBandLine() — that signed path is what makes the whole band
    // effect invertible.
    float mBandPolarity = 1.0f;

    // Accumulated band brightness modulation at a normalized radius.
    // Each active band contributes a raised-cosine bump: 1.0 at its
    // center, smoothly → 0 at ±halfWidth. Summed across bands so
    // overlapping pulses reinforce. Typical result 0..~2.
    float bandModAt(float rNorm) const
    {
      float mod = 0.0f;
      for (int b = 0; b < kMaxBands; b++)
      {
        if (!mBands[b].active)
          continue;
        float d = rNorm - mBands[b].pos;
        if (d < 0.0f)
          d = -d;
        const float hw = mBands[b].halfWidth;
        if (d < hw)
        {
          const float x = d / hw;   // 0 at band center .. 1 at edge
          mod += 0.5f * (1.0f + lutCosRad(3.14159265f * x));
        }
      }
      return mod;
    }

    // Per-pixel line raster with radial-band brightness modulation.
    // Walks the segment with a DDA stepper; for each pixel computes
    // its radius from the system center, evaluates bandModAt(), and
    // adds the band contribution to the depth-shaded base. gMinR2 /
    // gMaxR2 are the squared screen-radius bounds spanning ALL active
    // bands — pixels outside that annulus skip the sqrt and the
    // per-band loop entirely (the common case). Clips per pixel.
    void drawBandLine(od::FrameBuffer &fb,
                      int x0, int y0, int x1, int y1, int baseBright,
                      float fcx, float fcy, float invMaxR, float bandGain,
                      float gMinR2, float gMaxR2,
                      int clipL, int clipR, int clipB, int clipT) const
    {
      const int dx = x1 - x0, dy = y1 - y0;
      const int adx = dx < 0 ? -dx : dx;
      const int ady = dy < 0 ? -dy : dy;
      const int steps = adx > ady ? adx : ady;
      const float stepX = (steps > 0) ? (float)dx / (float)steps : 0.0f;
      const float stepY = (steps > 0) ? (float)dy / (float)steps : 0.0f;
      float fx = (float)x0, fy = (float)y0;
      for (int s = 0; s <= steps; s++)
      {
        const int px = (int)(fx + 0.5f);
        const int py = (int)(fy + 0.5f);
        int bright = baseBright;
        const float ddx = (float)px - fcx;
        const float ddy = (float)py - fcy;
        const float r2 = ddx * ddx + ddy * ddy;
        if (r2 >= gMinR2 && r2 <= gMaxR2)
        {
          // bandGain carries the polarity sign: +reveal brightens,
          // -obscure darkens. Dual clamp keeps the result on-scale
          // in both directions.
          const float rNorm = sqrtf(r2) * invMaxR;
          bright = baseBright + (int)(bandModAt(rNorm) * bandGain);
          if (bright > 15)
            bright = 15;
          if (bright < 0)
            bright = 0;
        }
        // Draw unconditionally within clip bounds. At Fold>0 the
        // background is bright and a brightness-0 pixel is the dark
        // figure (negative space), not a no-op against black — so it
        // must not be skipped.
        if (px >= clipL && px <= clipR && py >= clipB && py <= clipT)
          fb.pixel(bright, px, py);
        fx += stepX;
        fy += stepY;
      }
    }

    // Build the Fold contour-field frame cache (see the mFieldC /
    // mFieldS members). For every pixel, sum cos and sin of
    // (dₙ · kCoronaRingFreq) over all nodules — the phase-independent
    // halves of the angle-subtraction identity. This is the expensive
    // pass (per pixel: kCoronaNoduleCount × sqrt + cos + sin), but it
    // runs ONCE — lazily on first draw, and again only if the region
    // is ever resized. A sub-millisecond one-time hitch when the Mode
    // control is first shown.
    void buildFoldCache(int w, int h)
    {
      delete[] mFieldC;
      delete[] mFieldS;
      const int count = w * h;
      mFieldC = new float[count];
      mFieldS = new float[count];
      mFieldCacheW = w;
      mFieldCacheH = h;
      // Nodule positions are normalized; scale to region pixels once.
      float nodeX[kCoronaNoduleCount];
      float nodeY[kCoronaNoduleCount];
      for (int n = 0; n < kCoronaNoduleCount; n++)
      {
        nodeX[n] = kCoronaNoduleX[n] * (float)w;
        nodeY[n] = kCoronaNoduleY[n] * (float)h;
      }
      int i = 0;
      for (int py = 0; py < h; py++)
      {
        for (int px = 0; px < w; px++, i++)
        {
          float cAccum = 0.0f;
          float sAccum = 0.0f;
          for (int n = 0; n < kCoronaNoduleCount; n++)
          {
            const float dx = (float)px - nodeX[n];
            const float dy = (float)py - nodeY[n];
            const float d  = sqrtf(dx * dx + dy * dy);
            const float a  = d * kCoronaRingFreq;
            cAccum += lutCosRad(a);
            sAccum += lutSinRad(a);
          }
          mFieldC[i] = cAccum;
          mFieldS[i] = sAccum;
        }
      }
    }

    // Phase-3e Fold contour field — the background texture at Fold>0.
    // A multi-source radial-wave interference field: concentric rings
    // emanate from each kCoronaNodule* seed point, and rings from
    // neighbouring nodules merge/interfere — approximating a
    // reaction-diffusion / Turing-pattern look without a stateful RD
    // simulation. The summed cosine field is hard-thresholded to two
    // brightness levels (the reference look).
    //
    // The expensive part — Σ sqrt + cos/sin over the nodules — is
    // frame-INVARIANT except for the global ring `phase`, so it lives
    // in buildFoldCache's mFieldC / mFieldS tables. This per-frame
    // pass just reconstructs the sum via the angle-subtraction
    // identity (field = cosP·C + sinP·S) — a cheap MAC per pixel — and
    // thresholds. `phase` advances every frame (rings fan outward);
    // the post-fold envelope speeds that drift (in draw()) and deepens
    // `rippleDepth`, the gap between the two levels, on each hit.
    // Drawn per-pixel with fb.pixel (SET): fb.fill BLENDs (OR) and
    // can't do per-pixel variation anyway. At Fold=0 bgBase and
    // rippleDepth are both 0, so every pixel resolves to 0 — a flat
    // black field, identical to the pre-Phase-3e background.
    void drawFoldField(od::FrameBuffer &fb, int left, int bot,
                       int w, int h, float foldPos, float envLevel,
                       float phase) const
    {
      const int bgBase = (int)(foldPos * 15.0f);
      // Gap between the two contrast levels — breathes with the
      // envelope (resting foldPos·7, swelling to foldPos·13 on a
      // full-amplitude hit, so the dark troughs sit well below the
      // bright ridges). The foldPos factor pins Fold=0 to flat black
      // regardless of envelope.
      const int rippleDepth = (int)(foldPos * (7.0f + envLevel * 6.0f));
      const int darkLevel = (bgBase - rippleDepth < 0) ? 0
                                                       : bgBase - rippleDepth;
      // Angle-subtraction: Σcos(dₙ·freq − phase) = cosP·C + sinP·S,
      // with C/S precomputed per pixel in buildFoldCache.
      const float cosP = lutCosRad(phase);
      const float sinP = lutSinRad(phase);
      const float *fc = mFieldC;
      const float *fs = mFieldS;
      int i = 0;
      for (int py = 0; py < h; py++)
      {
        for (int px = 0; px < w; px++, i++)
        {
          const float field = fc[i] * cosP + fs[i] * sinP;
          fb.pixel(field > 0.0f ? bgBase : darkLevel, left + px, bot + py);
        }
      }
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

      // Background is filled later, once Fold is known — see the
      // Phase-3e Fold colour inversion block below.

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
      float attackPos = 0.0f;   // bipolar -1..+1 (noise / instant / slow)
      float decayPos  = 0.5f;   // 0..1
      float foldPos   = 0.0f;   // 0..1 — drives the Phase-3e colour inversion
      float envLevel  = 0.0f;   // 0..1 post-fold envelope — Fold field breathing
      if (mpVisadhara)
      {
        const float spreadPos = mpVisadhara->mSpread.value();
        const float modePos   = mpVisadhara->mMode.value();
        harmonicPos           = mpVisadhara->mHarmonic.value();
        morphPos              = mpVisadhara->mMorph.value();
        attackPos             = mpVisadhara->mAttack.value();
        decayPos              = mpVisadhara->mDecay.value();
        foldPos               = mpVisadhara->mFold.value();
        envLevel              = mpVisadhara->vizEnvLevel();
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
        if (attackPos < -1.0f) attackPos = -1.0f;
        if (attackPos > 1.0f) attackPos = 1.0f;
        if (decayPos < 0.0f) decayPos = 0.0f;
        if (decayPos > 1.0f) decayPos = 1.0f;
        if (foldPos < 0.0f) foldPos = 0.0f;
        if (foldPos > 1.0f) foldPos = 1.0f;
        if (envLevel < 0.0f) envLevel = 0.0f;
        if (envLevel > 1.0f) envLevel = 1.0f;
      }

      // --- Phase-3e Fold colour inversion ---
      // Fold inverts the whole viz: Fold=0 is bright wireframe on a
      // black field with reveal (brightening) bands; Fold=1 is dark
      // wireframe on a white field with obscure (darkening) bands.
      // Three coordinated moves:
      //   1. background: black → Fold contour field (here),
      //   2. wireframe shade: mirrored per-edge in the render loop,
      //   3. band polarity: +1 → −1 (mBandPolarity, into bandGain).
      // NOTE the figure does NOT mirror through 15−x. The 0..15
      // greyscale is perceptually lopsided — the dark end resolves
      // cleanly, the bright end does not (13 vs 15 is invisible). A
      // literal 15−x figure (tried in 2.6.2.33) put the Fold=1
      // wireframe at 6..13 on a 15 field, where it vanished into the
      // ground. Instead the figure shade mirrors WITHIN its own [2,9]
      // band (11−x): it stays dark and clearly readable at BOTH fold
      // extremes — light-on-dark at Fold=0, dark-on-light at Fold=1 —
      // and the depth shading flips (front edge bright ↔ dark) so the
      // static, no-band figure still visibly inverts. Staying inside
      // [2,9] also leaves the shockwave headroom to push past the
      // base in either direction. Mid-Fold is a low-contrast
      // crossover (figure ≈ ground, band polarity 0).
      //
      // The background is the Fold contour field (drawFoldField): a
      // multi-source radial-wave interference texture — concentric
      // rings emanating from a scattered set of nodules, merging
      // where they meet, hard-thresholded to two contrast levels. It
      // breaks up the harsh flat white at high Fold. The ring phase
      // advances every frame so the rings fan outward; the envelope
      // kicks that drift faster and deepens the contrast on each hit.
      // At Fold=0 it resolves to a flat black field, identical to the
      // prior behaviour.
      mFoldPhase += 0.05f + envLevel * 0.25f;
      if (mFoldPhase > 6.28318530718f) mFoldPhase -= 6.28318530718f;
      // Lazily (re)build the phase-independent contour cache — once on
      // first draw, again only if the region is ever resized.
      if (mFieldCacheW != w || mFieldCacheH != h)
        buildFoldCache(w, h);
      drawFoldField(fb, left, bot, w, h, foldPos, envLevel, mFoldPhase);
      mBandPolarity = 1.0f - 2.0f * foldPos;   // +1 reveal → −1 obscure

      // --- Phase-3d shockwave band emission + advance ---
      // Poll Visadhara's trigger counter. mLastTriggerCount < 0 means
      // we haven't synced to the unit yet — adopt the current count
      // silently so the first rendered frame doesn't fire a spurious
      // band. Thereafter any change emits one outward + one inward
      // band pair (emission capped at one pair per frame, so dense
      // trigger streams become a framerate-bound strobe).
      if (mpVisadhara)
      {
        const int tc = mpVisadhara->vizTriggerCount();
        if (mLastTriggerCount < 0)
        {
          mLastTriggerCount = tc;
        }
        else if (tc != mLastTriggerCount)
        {
          mLastTriggerCount = tc;

          // Decay → outward shockwave kinematics. Long decay = slow,
          // wide, languid sweep; short decay = fast, tight snap.
          // tInc is the per-frame lifetime-progress step (1/tInc =
          // band lifetime in frames); width sets the raised-cosine
          // half-extent.
          const float outTInc  = 0.014f + (1.0f - decayPos) * 0.048f;
          const float outWidth = 0.13f + decayPos * 0.13f;
          // Attack → inward collapse kinematics. Attack is bipolar;
          // fold to a 0..1 "slowness" (slow attack → slow, visible
          // converge; instant/negative attack → quick flash).
          float aSlow = (attackPos + 1.0f) * 0.5f;
          if (aSlow < 0.0f) aSlow = 0.0f;
          if (aSlow > 1.0f) aSlow = 1.0f;
          const float inTInc  = 0.023f + (1.0f - aSlow) * 0.073f;
          const float inWidth = 0.10f + aSlow * 0.12f;

          // Outward band — center → past the rim.
          Band &ob = mBands[mNextBandSlot];
          ob.t         = 0.0f;
          ob.tInc      = outTInc;
          ob.startPos  = 0.0f;
          ob.endPos    = 1.25f;
          ob.pos       = 0.0f;
          ob.halfWidth = outWidth;
          ob.active    = true;
          mNextBandSlot = (mNextBandSlot + 1) % kMaxBands;
          // Inward band — beyond the rim → through the center.
          Band &ib = mBands[mNextBandSlot];
          ib.t         = 0.0f;
          ib.tInc      = inTInc;
          ib.startPos  = 1.15f + inWidth;
          ib.endPos    = -inWidth;
          ib.pos       = ib.startPos;
          ib.halfWidth = inWidth;
          ib.active    = true;
          mNextBandSlot = (mNextBandSlot + 1) % kMaxBands;
        }
      }

      // Advance every active band: step its linear lifetime progress
      // t, retire it once t ≥ 1, otherwise refresh the cached eased
      // position. coronaEase() shapes the start→end travel so the
      // band decelerates as it goes (quadratic ease-out).
      for (int b = 0; b < kMaxBands; b++)
      {
        if (!mBands[b].active)
          continue;
        mBands[b].t += mBands[b].tInc;
        if (mBands[b].t >= 1.0f)
        {
          mBands[b].active = false;
          continue;
        }
        const float e = coronaEase(mBands[b].t);
        mBands[b].pos = mBands[b].startPos +
                        (mBands[b].endPos - mBands[b].startPos) * e;
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
      const float fcx = (float)cx;
      const float fcy = (float)cy;

      // Band reject bounds: the squared screen-radius annulus that
      // spans every active band. A pixel whose r² falls outside
      // [gMinR2, gMaxR2] gets no band modulation, so drawBandLine()
      // can skip its sqrt and per-band loop. maxR normalizes screen
      // radius to the band's 0..1 space (rim = R_petal + r_polygon,
      // the worst-case horizontal extent of the geometry).
      const float maxR = R_petal + r_polygon;
      const float invMaxR = (maxR > 1.0f) ? (1.0f / maxR) : 1.0f;
      float gMinR2 = 1.0e30f;
      float gMaxR2 = -1.0f;
      for (int b = 0; b < kMaxBands; b++)
      {
        if (!mBands[b].active)
          continue;
        float lo = (mBands[b].pos - mBands[b].halfWidth) * maxR;
        float hi = (mBands[b].pos + mBands[b].halfWidth) * maxR;
        if (lo < 0.0f) lo = 0.0f;
        if (hi < 0.0f) hi = 0.0f;
        const float lo2 = lo * lo;
        const float hi2 = hi * hi;
        if (lo2 < gMinR2) gMinR2 = lo2;
        if (hi2 > gMaxR2) gMaxR2 = hi2;
      }
      // Polarity-signed band strength: +reveal / -obscure. Computed
      // once per frame, passed into drawBandLine for the per-pixel
      // application. Phase 3e drives mBandPolarity from Fold.
      const float bandGain = kCoronaBandStrength * mBandPolarity;

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

        // Draw numVerts edges. Base brightness is the depth shade
        // (front edge bright, back edge dim), inverted by Fold —
        // mirrored WITHIN the figure's own [2,9] band (11−x), not
        // across the full 0..15 scale (see the Fold block above for
        // why). normalShade = 2 + depthN·7 (2..9, front bright) at
        // Fold=0 becomes invertShade = 11 − normalShade (9..2, front
        // dark) at Fold=1: the figure stays dark and readable at both
        // extremes, the depth shading flips, and the [2,9] bounds
        // leave the shockwave headroom past the base either way.
        // Every edge is rastered per-pixel through drawBandLine(),
        // which draws with fb.pixel (SET). fb.line cannot be used —
        // it BLENDs (bitwise-OR), so it can only lighten and could
        // never draw the dark Fold=1 wireframe onto the bright field.
        // When no band is active gMinR2/gMaxR2 reject every pixel and
        // drawBandLine simply sets baseBright — the band machinery is
        // a no-op, not a cost worth a separate path.
        for (int j = 0; j < numVerts; j++)
        {
          const int nj = (j + 1) % numVerts;
          const float midZ = (sz[j] + sz[nj]) * 0.5f;
          const float maxAbsZ = (R_petal + r_polygon) * cosTilt;
          float depthN = 0.5f + 0.5f * (midZ / maxAbsZ);
          if (depthN < 0.0f) depthN = 0.0f;
          if (depthN > 1.0f) depthN = 1.0f;
          const float normalShade = 2.0f + depthN * 7.0f;   // 2..9 (Fold=0, front bright)
          const float invertShade = 11.0f - normalShade;    // 9..2 (Fold=1, front dark)
          int baseBright =
              (int)(normalShade + (invertShade - normalShade) * foldPos);
          if (baseBright < 0) baseBright = 0;
          if (baseBright > 15) baseBright = 15;

          drawBandLine(fb, sx[j], sy[j], sx[nj], sy[nj], baseBright,
                       fcx, fcy, invMaxR, bandGain, gMinR2, gMaxR2,
                       left, left + w - 1, bot, bot + h - 1);
        }
      }
    }
#endif
  };
} // namespace stolmine
