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
#include "AnamNoise.h" // Perlin LUT for organic bubble blobs

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

    // Stable pseudo-random [0,1) from two ints (dendrite connector sites).
    inline float hash01(int a, int b)
    {
      unsigned int x = (unsigned int)(a * 374761393 + b * 668265263);
      x = (x ^ (x >> 13)) * 1274126177u;
      return (float)((x ^ (x >> 16)) & 0xffffff) / 16777216.0f;
    }

    inline float smooth01(float e0, float e1, float x)
    {
      if (e1 <= e0) return x >= e1 ? 1.0f : 0.0f;
      float t = (x - e0) / (e1 - e0);
      if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
      return t * t * (3.0f - 2.0f * t);
    }

    // Dendrite connectors: per-streamline site count grows with Density; each site
    // fades in over this window past its hash threshold (smooth branch-in).
    static const float kDendriteFade = 0.18f;
    static const float kDendriteLean = 3.0f; // px horizontal lean (the "crossing")

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
    static const int kStreamN  = 14;   // fixed streamline count -> 7 pair-bands for z-depth
    static const int kCtrlStep = 4;

    // CONNECTIONS (Density): the streamlines stay CLEAN; at drifting nodes along
    // the strip, a branch GROWS between the two lines bracketing the node as
    // Density rises (staggered per node). "Growth" (extent) represents a partial /
    // developing connection -- a stub reaching partway -> a full link; a soft
    // tip-fade smooths the growing end. Nodes drift with the flow (the axis of
    // change). L-system bifurcation can layer on later.
    static const float kConnSpacingX = 30.0f; // node spacing in content-x (px)
    static const float kConnDrift    = 6.0f;  // node drift speed with flow phase
    static const float kConnGrowWin  = 0.55f; // density window a branch grows 0..1 over
    static const float kConnLean     = 5.0f;  // px horizontal lean (organic angle)
    static const float kConnBow      = 4.0f;  // px curve bow

    // Streamline illumination (4-bit gray, 0..15). Base brightness scales with
    // Mix (dry = faint pond, wet = kBaseBright); droplet ring GLOW adds on top,
    // so ripples read as gradated brightness over the lines (visible rings at
    // full wet). Bending (geometry) stays independent of Mix.
    static const float kBaseBright = 9.0f;  // streamline brightness at full Mix
    static const float kBaseDim    = 3.0f;  // streamline brightness at Mix = 0
    static const float kGlowGain   = 1.8f;  // droplet ring illumination (NOT Mix-scaled
                                            // -> rings stay vivid at any wet, high contrast)

    // ---- Rain-on-pond ripples. A drop radiates as a TRAVELLING WAVE-TRAIN, not
    // a standing vibration: a few smooth Gaussian crests move outward together
    // (radii R, R-lambda, R-2lambda; R = c*age), so each surface point feels each
    // crest PASS ONCE -- a couple of gentle swells then calm -- instead of an
    // in-place carrier buzz. Amplitude ~ 1/sqrt(r) (energy spread round the
    // growing circle) * exp(-age/tau) (viscous decay). The flow lines bow around
    // the crest radii -> implied expanding concentric circles; drops superpose.
    static const float kRippleLambda = 11.0f; // base crest spacing (px)
    static const float kRippleSigma  = 3.2f;  // crest width (px); < lambda so distinct
    static const float kRippleSpread = 0.16f; // outer crests spaced wider (fake dispersion)
    static const float kRippleFan    = 4.0f;  // crests added per second of age (train fans out)
    static const int   kRippleMaxCrests = 6;
    static const float kRippleTrail  = 0.66f; // amplitude ratio per trailing crest
    static const float kRippleTau    = 1.20f; // temporal decay (s); life = 3*tau (long linger -> drops overlap & interact)
    static const float kRippleA0     = 8.0f;  // train source amplitude (tune w/ kRippleD)
    static const float kRippleD      = 2.4f;  // displacement gain (overall ripple strength)
    static const float kRippleEps    = 1.0f;  // r singularity guard (px)
    static const float kRippleLife   = 3.0f * kRippleTau;
    // Impact transient ("the plop"): a short, strong CENTRAL crater (-) that
    // releases into a rebound jet (+), seeding the first ring. Lives ~kImpactT s.
    static const float kImpactT       = 0.12f; // s (~7 frames @55fps)
    static const float kImpactA       = 6.0f;  // central amplitude (~1.5-2x a ring)
    static const float kImpactCraterW = 6.0f;  // crater width (px)
    static const float kImpactJetW    = 3.0f;  // jet width (px)
    // Knock-on: one delayed, weaker, finer secondary train (jet-rebound droplet).
    static const float kSecDelay      = 0.18f; // s after impact
    static const float kSecAmp        = 0.40f; // amplitude scale
    static const float kSecLam        = 0.70f; // wavelength scale (finer)

    // A dispersive crest train: the crest COUNT grows with age and the spacing
    // widens outward, so the ring pattern fans out over time (cheap dispersion).
    // Smooth Gaussian crests only (no carrier) -> each passes a point once.
    inline float crestTrain(float r, float R, float age, float a0, float lam0)
    {
      int nc = 2 + (int)(age * kRippleFan);
      if (nc > kRippleMaxCrests) nc = kRippleMaxCrests;
      const float inv2s2 = 1.0f / (2.0f * kRippleSigma * kRippleSigma);
      const float amp = (a0 / sqrtf(r)) * expf(-age / kRippleTau);
      float h = 0.0f, g = 1.0f, rc = R;
      for (int i = 0; i < nc; i++)
      {
        const float wf = r - rc;
        h += g * expf(-(wf * wf) * inv2s2);
        g *= kRippleTrail;
        rc -= lam0 * (1.0f + kRippleSpread * (float)i); // next crest back, wider gap
      }
      return amp * h;
    }

    // One droplet's effect on a flow-line point at offset (dx,dy): BEND (radial
    // Y push -> geometry) and GLOW (the ring height -> illumination over the
    // line). impact transient + dispersive primary train + one knock-on train.
    struct RippleHit { float bend; float glow; };
    inline RippleHit rippleEval(float dx, float dy, float age, float c)
    {
      RippleHit out;
      out.bend = 0.0f;
      out.glow = 0.0f;
      const float r2 = dx * dx + dy * dy;
      const float R = c * age;
      const float rhi = R + 3.0f * kRippleSigma;
      if (age >= kImpactT && r2 > rhi * rhi) return out; // ahead of the front
      const float r = sqrtf(r2) + kRippleEps;

      float impact = 0.0f;
      if (age < kImpactT) // the plop: crater dips, jet rebounds, then gone
      {
        const float nn = age / kImpactT;
        const float crater = -expf(-(r * r) / (kImpactCraterW * kImpactCraterW)) * (1.0f - nn);
        const float jet = expf(-(r * r) / (kImpactJetW * kImpactJetW)) * sinf(3.14159265f * nn);
        impact = kImpactA * (crater + 0.6f * jet) * expf(-age / kImpactT);
      }
      float train = crestTrain(r, R, age, kRippleA0, kRippleLambda); // primary
      if (age > kSecDelay)                                           // knock-on
      {
        const float a2 = age - kSecDelay;
        train += crestTrain(r, c * a2, a2, kRippleA0 * kSecAmp, kRippleLambda * kSecLam);
      }
      out.bend = kRippleD * (impact + train) * (dy / r); // radial geometry push
      out.glow = train > 0.0f ? train : 0.0f;            // ring crests illuminate
      return out;
    }

    // Content-stride between ply slices: 42px ply + 1px SpottedStrip gap.
    static const int kStride = 43;
    // Whole-strip content width: rain falls across all of it (free-reign drops).
    static const int   kVizPlies  = 6;
    static const float kVizStripW = (float)(kVizPlies * kStride); // 258px
    static const float kVizColH   = 64.0f; // viz column height (px); bubbles rise across it
    static const float kBubRise   = 9.0f;  // bubble rise speed (px/s, calm)
    // Bubbles as 2D METABALLS (lava-lamp): per z-LEVEL, a scalar field = animated
    // FBM noise + Gaussian bumps at the bubbles, traced by marching squares ->
    // smooth iso-contours that morph + split/join. Bubbles share a level so their
    // fields merge; levels interleave with the bands by z (depth weave).
    static const int   kBubLevels     = 3;     // bubble z-levels (interleave w/ bands)
    static const int   kMetaCell      = 3;     // finer grid -> resolve irregular lobes
    static const float kMetaThresh    = 0.50f; // iso-contour threshold (lower = bigger blobs)
    static const float kMetaBumpAmp   = 1.0f;  // Gaussian bump peak per bubble
    static const float kMetaSigmaK    = 1.30f; // bump sigma = radius * this (wider = bigger)
    static const float kMetaNoiseGain = 0.95f; // MULTIPLICATIVE noise: shape each bump by the
                                               // local topography -> irregular/compound + splits
    static const float kMetaNoiseFreq = 0.11f; // noise spatial freq (more features per blob = lobes)
    static const float kMetaMorph     = 0.12f; // noise scroll (slow; movement walks the topography)
    static const float kMetaSlew      = 0.12f; // field temporal slew (gentle morph, no wild jumps)

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
