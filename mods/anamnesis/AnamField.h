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
    // Diffusion -> a GLOW BLOOM around the Density BUBBLES (links Diffusion +
    // Density + bloom). The metaball field already falls off smoothly outside the
    // fill threshold T; that outer band is drawn as a graded aura, MAX-blended so
    // it only ever brightens. Because the levels composite BACK->FRONT, the bloom
    // lands over the streamlines + bubbles at z <= the blooming level and is
    // occluded by higher z (draw order = the z test). Lines stay sharp.
    // Diffusion=0 -> no bloom (skipped). Both scale with Diffusion 0..1.
    // Diffusion drives the bloom RADIUS (how far the glow spreads): 0 at
    // Diffusion=0 (band collapses -> NO bloom) up to kBloomBandMax at Diffusion=1,
    // with an EXPONENTIAL throw (subtle low, accelerating high). Near-edge
    // brightness is held at the bubble edge brightness (kBloomGain~1) and grades
    // to 0 outward, so the bloom joins the contour cleanly. The graded brightness
    // is ordered-DITHERED (Bayer 4x4) to break 4-bit quantization banding.
    static const float kBloomBandMax = 0.50f; // max bloom radius (field-units below T) at Diff=1
                                              // (=T -> bloomLo~0, the widest safe reach: bloom
                                              // fades to 0 at the field's outer fringe, no flood)
    static const float kBloomExp     = 1.60f; // exponential throw on Diffusion->bloom (lower =
                                              // reaches wider earlier in the knob)
    static const float kBloomGain    = 1.00f; // near-edge brightness as fraction of bubble edge
    // The per-pixel black FILL (v>T, blocky) and the marching-squares AA contour
    // (smooth) are two independent edges; the blocky fill can spill a pixel PAST
    // the smooth contour -> dark sliver between the bright rim and the bloom. Fix:
    // pull the black fill INWARD to Tcore = T + kRimInset and draw the band
    // (T, Tcore] as a bright rim (= bubB) buffering the contour from the black, so
    // black can never reach the smooth edge. Inset sized to buffer ~>=1px.
    static const float kRimInset     = 0.18f; // field-units the black fill sits inside the contour

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
    // bend = vertical (dy/r) component of the radial surface push; glow = ring
    // illumination; push = the full RADIAL magnitude (for shoving bubbles in 2D,
    // direction = (dx,dy)/r). bend = push * (dy/r) -> single source of truth.
    struct RippleHit { float bend; float glow; float push; };
    inline RippleHit rippleEval(float dx, float dy, float age, float c)
    {
      RippleHit out;
      out.bend = 0.0f;
      out.glow = 0.0f;
      out.push = 0.0f;
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
      out.push = kRippleD * (impact + train);   // radial magnitude (2D bubble shove)
      out.bend = out.push * (dy / r);           // vertical component -> line geometry
      out.glow = train > 0.0f ? train : 0.0f;   // ring crests illuminate
      return out;
    }

    // Content-stride between ply slices: 42px ply + 1px SpottedStrip gap.
    static const int kStride = 43;
    // Whole-strip content width: rain falls across all of it (free-reign drops).
    static const int   kVizPlies  = 6;
    static const float kVizStripW = (float)(kVizPlies * kStride); // 258px
    static const float kVizColH   = 64.0f; // viz column height (px); bubbles rise across it
    static const float kBubRise     = 9.0f;  // bubble rise speed (px/s, calm)
    static const float kBubSpawnInt = 0.25f; // min interval between spawns (s, clock-scaled)
    static const float kCalveProb   = 0.40f; // chance a new bubble CALVES off an existing one
                                             // (born beside it, drifting away -> own lifetime)
    static const float kFreezeDrift = 9.0f;  // px/s: when FROZEN bubbles drift in random
                                             // directions (from seed) instead of rising up
    // ---- Bubble PHYSICS: bubbles are carried by the flow current and shoved by
    // passing ripple fronts (see atom bubble-update). Velocity relaxes toward a
    // TARGET = buoyant rise + flow-carry + ripple-shove, with inertia (kBubResp)
    // so shapes get thrown off course then drift back.
    static const float kBubResp    = 2.0f;  // velocity relaxation rate (1/s): low =
                                            // inertial (momentum carries past), high = rigid
    static const float kFlowAdvect = 26.0f; // flow streamfunction gradient -> px/s carry
                                            // (bubbles ride the swirling current)
    static const float kRipplePush = 14.0f; // ripple radial ACCEL gain (impulse/Stokes
                                            // drift: passing crests accumulate net outward
                                            // shove; x mDropAmp -> loud captures throw harder)
    static const float kBubVMax    = 60.0f; // px/s velocity clamp (impulses accumulate)
    static const float kPushEps    = 2.0f;  // px finite-difference step for flow gradient
    // Bubbles as 2D METABALLS (lava-lamp): per z-LEVEL, a scalar field = animated
    // FBM noise + Gaussian bumps at the bubbles, traced by marching squares ->
    // smooth iso-contours that morph + split/join. Bubbles share a level so their
    // fields merge; levels interleave with the bands by z (depth weave).
    static const int   kBubLevels     = 3;     // bubble z-levels (interleave w/ bands)
    static const int   kMetaCell      = 3;     // finer grid -> resolve irregular lobes
    static const float kMetaThresh    = 0.50f; // iso-contour threshold (lower = bigger blobs)
    static const float kMetaBumpAmp   = 1.0f;  // Gaussian bump peak per bubble
    static const float kMetaSigmaK    = 1.00f; // bump sigma = radius * this (sharper -> pinches sooner)
    static const float kMetaNoiseGain = 0.45f; // MULTIPLICATIVE noise edge wobble (sub-bumps do the
                                               // compounding now, so this is lighter)
    static const float kMetaNoiseFreq = 0.11f; // noise spatial freq (more features per blob = lobes)
    // Each bubble = a CLUSTER of sub-bumps whose offsets are read from the noise
    // topography at the bubble (so they "walk the LUT" as it moves) -> compound
    // blobs that pinch and SPLIT. The compound contour is one iso-line; the field
    // slew makes the change fluid.
    // Lobe POINTS: a layer of drifting points (noise-driven, smooth motion). Any
    // bubble keeps a CORE bump and LATCHES a momentary lobe onto each point within
    // reach, weighted by distance (smooth fade-in/out). As points drift in/out the
    // lobes grow & shed -> compound shapes that pinch and SPLIT. Distance (reach)
    // is the lever; bigger shapes reach further (-> more lobes).
    static const int   kNumPoints      = 28;    // points in the drifting layer
    static const float kPointDrift     = 22.0f; // px each point wanders from its base
    static const float kPointDriftRate = 0.28f; // point drift speed (x flow phase)
    static const int   kMaxLobes       = 7;     // max lobes a bubble can latch (higher -> fewer cap-swap pops)
    static const float kLatchK         = 2.00f; // reach = radius * this + base (big -> lobes reach far)
    static const float kLatchBase      = 7.0f;  // px base reach (small bubbles still latch)
    static const float kLatchFull      = 0.78f; // lobe at FULL strength out to reach*this, then fades
    // Reach BREATHES with a slow noise -> occasionally extends to grab a distant
    // point (a lobe shoots far out -> more dramatic separation), smoothly.
    // Per-bubble point weighting: AFFINITY (this bubble's pull to a point, gated by
    // kAffBias so each ignores some points) and per-point STRENGTH (-> lobe size).
    static const float kAffBias        = 0.35f; // affinity below this -> point not latched
    static const float kPointStrMin    = 0.40f; // min per-point strength (lobe size floor)
    static const float kReachVar       = 0.75f; // reach *= 1 + this*noise (breadth of breathing)
    static const float kReachFreq      = 0.04f; // reach-noise spatial freq
    static const float kReachRate      = 0.18f; // reach-noise drift speed (x flow phase)
    static const float kCoreK          = 0.65f; // core bump sigma = radius * this
    static const float kLobeR          = 3.2f;  // lobe sub-bump radius (px)
    static const float kMetaMorph     = 0.12f; // noise scroll (slow; movement walks the topography)
    static const float kMetaSlew      = 0.09f; // field temporal slew (gentle morph, no wild jumps)

    // Baseline y (px) of streamline s within a height-h column.
    inline float baseline(int s, int n, int h)
    {
      return ((float)s + 0.5f) * (float)h / (float)n;
    }

    // Smooth, x-continuous vertical displacement of the flowing line at
    // content-x `cx`, baseline `yb`, animation `phase`. A sum of traveling
    // sines -> a gentle braided current. Continuous in cx (no per-ply term),
    // so neighboring slices meet exactly at the seam.
    // Size shapes the FLOW FEATURE SCALE: small Size = tight short-wavelength
    // ripples; large Size = broad long swells (slightly taller). `size` is the
    // global vizSize() 0..1, so every ply scales identically -> seams stay
    // continuous. Only the SPATIAL terms scale (phase stays outside -> motion
    // speed is unchanged; Size changes feature size, not tempo).
    static const float kSizeFreqTight = 2.13f; // spatial-freq mult at Size=0 (tight; ~1.25x throw)
    static const float kSizeFreqWide  = 0.40f; // spatial-freq mult at Size=1 (broad swell; ~1.25x throw)
    static const float kSizeAmpMin    = 0.85f; // swell amplitude mult at Size=0
    static const float kSizeAmpMax    = 1.20f; // swell amplitude mult at Size=1
    // Flow MOTION stays one-directional (the wave is linear in cx -> a single
    // left<->right scroll). The Size ADJUSTMENT is anchored at the strip CENTER:
    // the spatial coordinate is xc = cx - center, so the frequency scaling pivots
    // about the middle -- at cx=center the scaled spatial term is 0 for ANY Size,
    // so that column is fixed and features compress toward / expand from the
    // centre on BOTH sides as Size changes (epicentre of the size effect),
    // WITHOUT making the continuous flow travel two ways. Offsetting by a global
    // constant doesn't change travel direction (just a constant phase shift) and
    // cx + center are global -> seams stay continuous.
    static const float kFlowCenter = kVizStripW * 0.5f; // strip centre (Size pivot)
    inline float flow(float cx, float yb, float phase, float size)
    {
      const float fsc = kSizeFreqTight + (kSizeFreqWide - kSizeFreqTight) * size;
      const float asc = kSizeAmpMin + (kSizeAmpMax - kSizeAmpMin) * size;
      const float xc = cx - kFlowCenter; // anchor Size scaling at centre (linear -> one-way flow)
      float d = 0.0f;
      d += 2.6f * sinf(fsc * (0.055f * xc + yb * 0.045f) + phase);
      d += 1.3f * sinf(fsc * (0.115f * xc + yb * 0.090f) - 0.70f * phase);
      d += 0.7f * sinf(fsc * (0.210f * xc) + 1.30f * phase);
      return d * asc;
    }

  } // namespace field
} // namespace anamnesis
