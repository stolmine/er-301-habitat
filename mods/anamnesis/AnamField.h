// anamnesis::field -- the shared "Pond of Recollection" flow-field math.
//
// One continuous streamline field painted across Anamnesis's whole main-display
// ply strip (planning/spatial-glitch-impl/07-allover-viz.md). Every ply renders
// its 42px window of the SAME field, indexed by a CONTENT x-coordinate (pixels
// across the entire strip), so adjacent ply slices align at their seams by
// construction. Header-only, pure math, no od deps -- safe to include anywhere.
//
// Phase 5b foundation: the baseline braided "current". Per-ply features
// (ripples / vortex / crystal / moire / fade) compose on top in Phase C.

#pragma once

#include <math.h>

namespace anamnesis
{
  namespace field
  {

    // Per-ply feature motifs (composed on top of the shared current). Passed
    // from Lua as the AnamFieldGraphic `feature`; 0 = plain current only.
    namespace feature
    {
      enum
      {
        kPlain = 0,
        kLooper,  // raindrop ripples from the playhead
        kFreeze,  // crystallize / lock when frozen
        kSize,    // vortex scaled by Size
        kDensity, // sparse -> dense moire wash
        kClock,   // grit-dashes (flow-rate already global via mVizPhase)
        kMix      // fade (dry) / flood (wet)
      };
    }

    inline float fract(float x) { return x - floorf(x); }

    // Catmull-Rom through 4 control points -> smooth flow-line interpolation.
    inline float catmull(float p0, float p1, float p2, float p3, float t)
    {
      const float t2 = t * t, t3 = t2 * t;
      return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                     (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                     (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    }

    // ---- Flow lines. kStreamlines is THE density lever (more lines = more
    // obvious collective motion); kept easy to retune. Control points every
    // kCtrlStep px are Catmull-Rom interpolated to per-pixel y, so the per-line
    // cost (flow + ripple evals) stays low even as the count grows.
    static const int kStreamlines = 12;
    static const int kCtrlStep    = 4;

    // ---- Rain-on-pond ripples. A drop radiates as a TRAVELLING WAVE-TRAIN, not
    // a standing vibration: a few smooth Gaussian crests move outward together
    // (radii R, R-lambda, R-2lambda; R = c*age), so each surface point feels each
    // crest PASS ONCE -- a couple of gentle swells then calm -- instead of an
    // in-place carrier buzz. Amplitude ~ 1/sqrt(r) (energy spread round the
    // growing circle) * exp(-age/tau) (viscous decay). The flow lines bow around
    // the crest radii -> implied expanding concentric circles; drops superpose.
    static const int   kRippleCrests = 3;     // leading crest + 2 trailing rings
    static const float kRippleLambda = 11.0f; // spacing between crests (px)
    static const float kRippleSigma  = 3.2f;  // crest width (px); < lambda so distinct
    static const float kRippleTrail  = 0.62f; // amplitude ratio per trailing crest
    static const float kRippleTau    = 0.85f; // temporal decay (s); life = 3*tau (slow linger)
    static const float kRippleA0     = 8.0f;  // source amplitude (tune w/ kRippleD)
    static const float kRippleD      = 1.5f;  // displacement gain (overall ripple strength)
    static const float kRippleEps    = 1.0f;  // r singularity guard (px)
    static const float kRippleLife   = 3.0f * kRippleTau;

    // Vertical (Y) displacement contributed by one droplet to a flow-line point
    // at offset (dx,dy) from the epicenter. Sum of smooth crests (no carrier) ->
    // a clean passing swell. 0 outside the train's band (squared-dist reject).
    inline float rippleDispY(float dx, float dy, float age, float c, float phase)
    {
      const float r2 = dx * dx + dy * dy;
      const float R = c * age;
      const float band = 3.0f * kRippleSigma;
      float rlo = R - (float)(kRippleCrests - 1) * kRippleLambda - band;
      if (rlo < 0.0f) rlo = 0.0f;
      const float rhi = R + band;
      if (r2 > rhi * rhi) return 0.0f;
      if (rlo > 0.0f && r2 < rlo * rlo) return 0.0f;
      const float r = sqrtf(r2) + kRippleEps;
      const float inv2s2 = 1.0f / (2.0f * kRippleSigma * kRippleSigma);
      float h = 0.0f, g = 1.0f;
      for (int j = 0; j < kRippleCrests; j++)
      {
        const float wf = r - (R - (float)j * kRippleLambda);
        h += g * expf(-(wf * wf) * inv2s2);
        g *= kRippleTrail;
      }
      const float A = (kRippleA0 / sqrtf(r)) * expf(-age / kRippleTau);
      return kRippleD * A * h * (dy / r); // Y component of the radial swell
    }

    // Content-stride between ply slices: 42px ply + 1px SpottedStrip gap.
    static const int kStride = 43;

    // Baseline y (px) of streamline s within a height-h column.
    inline float baseline(int s, int n, int h)
    {
      return ((float)s + 0.5f) * (float)h / (float)n;
    }

    // Smooth, x-continuous vertical displacement of the flowing line at
    // content-x `cx`, baseline `yb`, animation `phase`. A sum of traveling
    // sines -> a gentle braided current. Continuous in cx (no per-ply term),
    // so neighboring slices meet exactly at the seam.
    inline float flow(float cx, float yb, float phase)
    {
      float d = 0.0f;
      d += 2.6f * sinf(0.055f * cx + phase + yb * 0.045f);
      d += 1.3f * sinf(0.115f * cx - 0.70f * phase + yb * 0.090f);
      d += 0.7f * sinf(0.210f * cx + 1.30f * phase);
      return d;
    }

  } // namespace field
} // namespace anamnesis
