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
    // NOTE: the mixing arithmetic must be UNSIGNED. As signed int it overflows
    // (UB) and gcc -O3 provably exploits it (-Waggressive-loop-optimizations
    // fired on the buildFieldFrame point loop). Unsigned wraparound is
    // bit-identical to the two's-complement behavior the -Os builds had.
    inline float hash01(int a, int b)
    {
      unsigned int x = (unsigned int)a * 374761393u + (unsigned int)b * 668265263u;
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

    // ---- polynomial / fast transcendental kernels -------------------------
    // sin(a) for a in [-pi/2, pi/2]: odd Taylor through x^9, |err| < 4e-6 vs
    // libm (-108 dB, at the interval ends only). No runtime libm trig in
    // package DSP on am335x (feedback_package_trig_lut).
    inline float polySinQ(float a)
    {
      const float x2 = a * a;
      return a * (1.0f + x2 * (-0.16666667f + x2 * (0.00833333333f
                    + x2 * (-1.98412698e-4f + x2 * 2.75573192e-6f))));
    }
    // sin(y) for y in [0, 2*pi (+ a step)]: quadrant-fold onto polySinQ.
    // The middle branch computes P(pi - y), bit-identical to -P(y - pi)
    // because P is odd and IEEE subtraction is exactly anti-symmetric.
    inline float polySin2Pi(float y)
    {
      float x;
      if (y < 1.57079633f)      x = y;
      else if (y < 4.71238898f) x = 3.14159265f - y;
      else                      x = y - 6.28318531f;
      return polySinQ(x);
    }
    // sin(y) for ANY finite y (flow-field args reach the thousands): reduce
    // by floor(y * 1/2pi) then polySin2Pi. At |y| ~ 6500 the float reduction
    // itself carries ~4e-4 rad of rounding -- on the flow field that moves a
    // ~100 px wave by ~0.006 px, far below one 4-bit AA quantum (proven by
    // the framebuffer A/B). DRAW-PATH ONLY.
    inline float fastSin(float y)
    {
      const float r = y * 0.15915494309f; // y / 2pi
      int k = (int)r; if ((float)k > r) k--;      // floor for either sign
      return polySin2Pi((r - (float)k) * 6.28318531f);
    }
    // e^x for x <= 0 (Gaussian falloffs / decay envelopes): 2^t split into
    // integer exponent (bit-assembled) + degree-4 mantissa poly. Relative
    // error < 3.1e-4 (the Vitrail fastExp2 kernel, float form); below -40
    // returns 0 (e^-40 ~ 4e-18). DRAW-PATH ONLY -- the audio-thread bubble
    // physics keeps libm expf so its trajectories are bit-untouched.
    inline float fastExpNeg(float x)
    {
      if (x < -40.0f) return 0.0f;
      const float t = x * 1.44269504f;    // x * log2(e), in (-58, 0]
      int xi = (int)t; if ((float)xi > t) xi--;   // floor
      const float f = t - (float)xi;
      const float p = 1.0f + f * (0.6931472f + f * (0.2402265f
                        + f * (0.0555041f + f * 0.0096181f)));
      union { uint32_t u; float f2; } e; e.u = (uint32_t)(xi + 127) << 23;
      return p * e.f2;
    }
    // FAST-selectable wrappers so the exact (audio-thread) and fast
    // (draw-thread) variants of the field formulas below share ONE source.
    template <bool FAST> inline float expT(float x)   { return FAST ? fastExpNeg(x) : expf(x); }
    template <bool FAST> inline float sinT(float y)   { return FAST ? fastSin(y) : sinf(y); }

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
    // Diffusion -> a GLOW around the Density BUBBLES (links Diffusion + Density +
    // bloom). The metaball field falls off smoothly outside the fill threshold T;
    // the band [bloomLo, T) is drawn as a graded aura, MAX-blended so it only ever
    // brightens. Levels composite BACK->FRONT, so the glow lands over streamlines +
    // bubbles at z <= the blooming level and is occluded by higher z. Lines stay
    // sharp.
    //
    // EDGE/GLOW pipeline (no rim band -- it popped in at Diff=0 and seamed against
    // the dithered glow). Instead:
    //  - INTERIOR occlusion is ANTI-ALIASED ("feathered"): pixel *= (1 - cov),
    //    cov = clamp((v - T)/kEdgeSoft). Deep interior -> full black (occludes);
    //    near the contour it feathers, so the blocky fill never spills a hard pixel
    //    past the smooth marching-squares contour (kills the dark sliver) with NO
    //    separate rim band. The crisp contour still draws on top as the bright edge.
    //  - The GLOW PEAK is HELD at the edge brightness (kBloomGain~1) so it joins the
    //    contour seamlessly; Diffusion drives only the RADIUS (expo throw), so at
    //    Diff=0+ the glow is a sub-pixel ring hugging the edge and widens OUTWARD
    //    smoothly -> no pop, smooth fade-in, and a clean edge->glow transition.
    //  - DITHER is Interleaved Gradient Noise (Jimenez), much smoother on gradients
    //    than an ordered Bayer matrix -> the faint tail reads as a soft glow, and
    //    the edge->glow texture transition is seamless.
    static const float kBloomBandMax = 0.50f; // max glow radius (field-units below T) at Diff=1
    static const float kBloomExp     = 1.80f; // exponential throw on Diffusion->glow radius
    static const float kBloomGain    = 1.00f; // glow peak as fraction of bubble edge (held -> clean join)
    static const float kEdgeSoft     = 0.18f; // field-units over which the interior black feathers (AA)

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
    // Decay -> ripple PERSISTENCE: scales the temporal decay tau, so rings fade
    // slower and reach further before dissolving (life = 3*tau scales with it).
    // Expo throw (subtle low end) + capped, and bounded by the fixed drop pool so
    // it can't run to soup.
    static const float kDecayTauMax  = 2.6f;  // max tau multiplier at Decay=1
    static const float kDecayExp     = 1.8f;  // exponential throw (subtle low end)
    inline float rippleTauOf(float decay)
    {
      if (decay <= 0.0f) return kRippleTau;
      return kRippleTau * (1.0f + (kDecayTauMax - 1.0f) * powf(decay, kDecayExp));
    }
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
    template <bool FAST>
    inline float crestTrainT(float r, float R, float age, float a0, float lam0, float tau)
    {
      int nc = 2 + (int)(age * kRippleFan);
      if (nc > kRippleMaxCrests) nc = kRippleMaxCrests;
      const float inv2s2 = 1.0f / (2.0f * kRippleSigma * kRippleSigma);
      const float amp = (a0 / sqrtf(r)) * expT<FAST>(-age / tau);
      float h = 0.0f, g = 1.0f, rc = R;
      for (int i = 0; i < nc; i++)
      {
        const float wf = r - rc;
        h += g * expT<FAST>(-(wf * wf) * inv2s2);
        g *= kRippleTrail;
        rc -= lam0 * (1.0f + kRippleSpread * (float)i); // next crest back, wider gap
      }
      return amp * h;
    }
    inline float crestTrain(float r, float R, float age, float a0, float lam0, float tau)
    { return crestTrainT<false>(r, R, age, a0, lam0, tau); }

    // One droplet's effect on a flow-line point at offset (dx,dy): BEND (radial
    // Y push -> geometry) and GLOW (the ring height -> illumination over the
    // line). impact transient + dispersive primary train + one knock-on train.
    // bend = vertical (dy/r) component of the radial surface push; glow = ring
    // illumination; push = the full RADIAL magnitude (for shoving bubbles in 2D,
    // direction = (dx,dy)/r). bend = push * (dy/r) -> single source of truth.
    struct RippleHit { float bend; float glow; float push; };
    template <bool FAST>
    inline RippleHit rippleEvalT(float dx, float dy, float age, float c, float tau)
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
        const float crater = -expT<FAST>(-(r * r) / (kImpactCraterW * kImpactCraterW)) * (1.0f - nn);
        // jet arg = pi*nn in [0, pi] -> polySin2Pi is exact-domain for FAST
        const float jetSin = FAST ? polySin2Pi(3.14159265f * nn) : sinf(3.14159265f * nn);
        const float jet = expT<FAST>(-(r * r) / (kImpactJetW * kImpactJetW)) * jetSin;
        impact = kImpactA * (crater + 0.6f * jet) * expT<FAST>(-age / kImpactT);
      }
      float train = crestTrainT<FAST>(r, R, age, kRippleA0, kRippleLambda, tau); // primary
      if (age > kSecDelay)                                                       // knock-on
      {
        const float a2 = age - kSecDelay;
        train += crestTrainT<FAST>(r, c * a2, a2, kRippleA0 * kSecAmp, kRippleLambda * kSecLam, tau);
      }
      out.push = kRippleD * (impact + train);   // radial magnitude (2D bubble shove)
      out.bend = out.push * (dy / r);           // vertical component -> line geometry
      out.glow = train > 0.0f ? train : 0.0f;   // ring crests illuminate
      return out;
    }
    // Exact form: audio-thread bubble physics (trajectories bit-untouched).
    inline RippleHit rippleEval(float dx, float dy, float age, float c, float tau)
    { return rippleEvalT<false>(dx, dy, age, c, tau); }
    // Fast form: draw-path band bending/glow only (< 3.1e-4 relative on ~8 px
    // amplitudes = milli-pixels; proven invisible by the framebuffer A/B).
    inline RippleHit rippleEvalFast(float dx, float dy, float age, float c, float tau)
    { return rippleEvalT<true>(dx, dy, age, c, tau); }

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

    // Strip-wide metaball grid dims for the shared per-frame field cache (Item 1
    // of planning/anamnesis-viz-optimization.md). ONE global grid over the whole
    // strip in CONTENT space (cell i,j -> content-x i*kMetaCell, content-y
    // j*kMetaCell), built once per frame on the op, sampled per-ply. +2 margin so
    // bilinear/marching-squares reads at the last column stay in-bounds.
    static const int kFieldGW = (kVizPlies * kStride) / kMetaCell + 2; // ~88 (258/3+2)
    static const int kFieldGH = 64 / kMetaCell + 2;                    // ~23 (kVizColH=64)

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
    // Mod -> a SLOW, BROAD, ORGANIC wander layered on the flow: low-frequency
    // noise (in cx, so global -> seams align) scrolling slowly (x mVizPhase, so
    // Freeze halts it too). Distinct channel from the traveling sines -- the whole
    // field (lines + the bubbles riding it) gently sways. Depth scales with Mod.
    static const float kModDepth = 7.0f;   // px vertical wander at Mod=1
    static const float kModSpace = 0.010f; // wander spatial freq (low -> broad sway)
    static const float kModRate  = 0.16f;  // wander scroll speed (slow, x flow phase)
    static const float kModExp   = 2.0f;   // exponential throw -> subtle on the low end
    template <bool FAST>
    inline float flowT(float cx, float yb, float phase, float size, float mod)
    {
      const float fsc = kSizeFreqTight + (kSizeFreqWide - kSizeFreqTight) * size;
      const float asc = kSizeAmpMin + (kSizeAmpMax - kSizeAmpMin) * size;
      const float xc = cx - kFlowCenter; // anchor Size scaling at centre (linear -> one-way flow)
      float d = 0.0f;
      d += 2.6f * sinT<FAST>(fsc * (0.055f * xc + yb * 0.045f) + phase);
      d += 1.3f * sinT<FAST>(fsc * (0.115f * xc + yb * 0.090f) - 0.70f * phase);
      d += 0.7f * sinT<FAST>(fsc * (0.210f * xc) + 1.30f * phase);
      float out = d * asc;
      if (mod > 0.0f) // slow organic wander (noise ~[-1,1]); not size-scaled; expo throw
      {
        // kModExp == 2.0f exactly, so the FAST path's mod*mod is the same
        // power law (-ffast-math folds the exact path's powf identically).
        const float modw = FAST ? (mod * mod) : powf(mod, kModExp);
        out += modw * kModDepth *
               anamnesis::noise::sample(cx * kModSpace, yb * kModSpace + phase * kModRate);
      }
      return out;
    }
    // Exact form: audio-thread bubble advection (trajectories bit-untouched).
    inline float flow(float cx, float yb, float phase, float size, float mod)
    { return flowT<false>(cx, yb, phase, size, mod); }
    // Fast form: draw-path band geometry only (phase error ~4e-4 rad at the
    // largest args = ~0.006 px on a 100 px wave; identical at every seam
    // because all plies evaluate the same formula on the same global grid).
    inline float flowFast(float cx, float yb, float phase, float size, float mod)
    { return flowT<true>(cx, yb, phase, size, mod); }

  } // namespace field
} // namespace anamnesis
