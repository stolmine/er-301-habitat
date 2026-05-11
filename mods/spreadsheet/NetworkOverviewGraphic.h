#pragma once

// Network overview viz — listener-centered sphere with per-tap
// trails and sonar pings.
//
// Tap positions on the sphere combine an LDS latitude (for even
// spread at any density) with an azimuth taken from the tap's
// actual reflector position (for illustrative faithfulness):
//   latitude_u = fmod(t × 1/φ + 0.5, 1)   in [0, 1)
//   zHat       = 2 × latitude_u − 1       in [−1, +1]
//   longitude  = atan2(reflector_y, reflector_x)
// Latitude via the golden-ratio low-discrepancy sequence on t gives
// every prefix of taps a uniform sphere coverage — density sweeps
// reveal new taps at well-distributed latitudes instead of filling
// a single band. Longitude from the reflector's actual disc azimuth
// means seed regeneration moves taps around the sphere, and as the
// sphere counter-rotates with listener motion the taps appear to
// revolve around the listener marker tracking real azimuth. Glitch
// Z displacement scales sphere radius per tap (lifts/pushes
// radially off the surface).
//
// Listener motion drives an additional sphere rotation so as the
// listener orbits the audio field, the sphere visually responds.
//
// Sonar pings emanate from view center; ring crossings against
// each reflector's ACTUAL world-space distance from listener
// trigger flashes (preserving audio time-of-flight in the temporal
// dimension while sphere position handles the spatial dimension).
//
// Per-tap trails: each frame, each tap deposits a low-brightness
// pixel into the persistence buffer at its current screen
// position. As the sphere rotates, the tap moves on screen and
// leaves a fading arc behind it.
//
// Layered render:
//   1. Persistence buffer fade.
//   2. Stutter wrap → ghost spawn into persistence.
//   3. Sonar ping update + reflector cross detection.
//   4. Rotation advance.
//   5. Clear fb.
//   6. Per-tap trail deposit + persistence render.
//   7. Render ping rings.
//   8. Render listener trace.
//   9. Render tap dots.
//   10. Render listener marker at center.

#include <od/graphics/Graphic.h>
#include <math.h>
#include <string.h>
#include "Network.h"

namespace stolmine
{
  class NetworkOverviewGraphic : public od::Graphic
  {
  public:
    NetworkOverviewGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpNetwork(0)
    {
      memset(mPixels, 0, sizeof(mPixels));
      memset(mLastStutterIter, 0, sizeof(mLastStutterIter));
      memset(mTapPingFlash, 0, sizeof(mTapPingFlash));
      // Sentinel inits for smoothing state. The ER-301 framework
      // appears to reconstruct NetworkOverviewGraphic periodically
      // (evidence: at motion=0, glitch=0 only tap dots dim while
      // the WHITE listener marker and ping rings stay full
      // brightness — tap dots are the only thing reading from
      // smoothed per-instance state). Out-of-range sentinels make
      // the first use after each re-init snap to the current
      // target instead of ramping from a stale init value, so the
      // re-instantiation is invisible. Valid ranges: shellLevel
      // [0,1], tapZ [-1,+1], brightness [0,15].
      for (int i = 0; i < kMaxNetworkTaps; i++) mShellLevel[i] = -1.0f;
      for (int i = 0; i < kMaxNetworkTaps; i++) mTapZSmoothed[i] = -2.0f;
      for (int i = 0; i < kMaxNetworkTaps; i++) mStutterFlicker[i] = 255;
      for (int i = 0; i < kMaxNetworkTaps; i++) mTapBrightSmoothed[i] = -1.0f;
      mPersistFadeCounter = 0;
      mRotAngle = 0.0f;
      mFrameCounter = 0;
      mPingCount = 0;
      mPingPhase = 0.0f;
      mSphereRotAccumulator = 0.0f;
      mPingSpawnCount = 0;
      // Seed motion-perturbation pattern with the object address so
      // each unit insertion gets a distinct but stable wobble.
      mPerturbSeed =
        (uint32_t)((uintptr_t)this * 2654435761u) ^ 0xA17BCD5Eu;
      for (int i = 0; i < kMaxPings; i++)
      {
        mPings[i].age = 0;
        mPings[i].axisX = 0.0f;
        mPings[i].axisY = 1.0f;
        mPings[i].axisZ = 0.0f;
      }
    }

    virtual ~NetworkOverviewGraphic()
    {
      if (mpNetwork) mpNetwork->release();
    }

    void follow(Network *p)
    {
      if (mpNetwork) mpNetwork->release();
      mpNetwork = p;
      if (mpNetwork) mpNetwork->attach();
    }

#ifndef SWIGLUA
  private:
    Network *mpNetwork;
    static const int kMaxW = 64;
    static const int kMaxH = 64;
    uint8_t mPixels[kMaxW * kMaxH];
    uint8_t mLastStutterIter[kMaxNetworkTaps];

    float mRotAngle;
    int   mFrameCounter;

    static const int   kMaxPings        = 4;
    static const int   kPingMaxAge      = 90;
    static const int   kPingFlashFrames = 12;
    // Each ping carries an axis vector (normal to its ring plane).
    // The ring is the great circle of the wavefront sphere in the
    // plane perpendicular to this axis — so different pings appear
    // at different "pitches" relative to the viz sphere, giving a
    // planetary-rings feel as they emanate from center outward.
    struct Ping { int age; float axisX, axisY, axisZ; };
    Ping mPings[kMaxPings];
    int  mPingCount;
    int  mPingSpawnCount;

    // Sonar ping phase accumulator. Each frame the phase advances
    // by motion × kPingRatePerFrame. When it crosses 1.0, one ping
    // is spawned. This is a CONSTANT-rate clock (rate ∝ motion)
    // independent of the walker's smoothed-random instantaneous
    // velocity, so the user perceives a steady cadence that
    // simply runs slower at low motion and faster at high motion.
    float mPingPhase;

    // Sphere rotation accumulator (replaces the walker-phase-driven
    // sphereRot). Advances at a constant rate scaled by motion, so
    // the sphere counter-rotates smoothly with no jerk. Decoupled
    // from the walker's smoothed-random velocity which oscillates,
    // reverses, and pauses — that chaos is correct for audio
    // modulation but wrong for visual rotation.
    float mSphereRotAccumulator;

    uint8_t mTapPingFlash[kMaxNetworkTaps];

    // Per-tap shell-membership fade (viz-side smoother).
    // 0 = not in feedback set, 1 = fully in. Rises when audio
    // fbWeight is nonzero, falls when zero. Asymmetric rates: in
    // ~200ms (12 frames), out ~300ms (18 frames) at 60fps. Used
    // both for line brightness and front-hemisphere shell
    // membership so leaving/entering taps fade rather than pop.
    float mShellLevel[kMaxNetworkTaps];

    // Per-tap smoothed Z displacement. Raw tapZ() jumps when the
    // walker wraps and the mode mutex reshuffles (NORMAL -> MUTE
    // -> CRUSH ...), which previously caused dozens of taps to
    // pop radially simultaneously. Smoothing ramps the radial
    // transition over ~50ms so the cycle reshuffle reads as a
    // soft morph rather than a hard reset.
    float mTapZSmoothed[kMaxNetworkTaps];

    // Counter for decay-scaled persistence-buffer fade. Higher
    // decay -> longer interval between fade steps -> longer
    // comet tails on tap dots.
    int mPersistFadeCounter;

    // Per-stutter-tap flicker counter — frames since the last
    // stutter loop iteration started (i.e., curIter just
    // decremented). On iter start the tap dot flashes bright,
    // then settles to a dim base brightness, producing a
    // flicker-with-ghost-trail look as the persistence buffer
    // retains a fading dot at each iteration position.
    uint8_t mStutterFlicker[kMaxNetworkTaps];

    // Per-tap smoothed base brightness (mode-derived). At every
    // walker wrap the mode mutex reshuffles all active taps
    // simultaneously and the mode-based brightness target jumps
    // (NORMAL=9, MUTE=3, etc.). Without smoothing this pops
    // instantly and reads as "everything resets at once." With
    // ~70ms smoothing the transitions read as a soft fade.
    // STUTTER mode bypasses this so its 2-frame flash stays
    // instant.
    float mTapBrightSmoothed[kMaxNetworkTaps];

    // Per-instance seed for the motion-driven tap perturbation.
    // Initialized in the constructor from the object address so
    // each unit insertion gets a different but stable wobble
    // pattern. tapToSphere uses this + tap index to hash a
    // constant per-tap (dx, dy, dz) offset; motion scales how
    // much of that offset is applied.
    uint32_t mPerturbSeed;

    // Map tap to a 3D point on the unit sphere (see file header).
    // Latitude: golden-ratio LDS on tap index (uniform spread at any
    // density). Longitude: actual reflector azimuth (illustrative).
    // zGlitch lifts the point radially off the sphere surface.
    // motion linearly blends in a per-tap, per-instance deterministic
    // (dx, dy, dz) offset that disturbs the Fibonacci distribution —
    // at motion=0 the layout is pure Fibonacci; at motion=1 the
    // sphere is fully wobbled along its session-seeded pattern.
    inline void tapToSphere(int t, float zGlitch,
                            float *sx, float *sy, float *sz) const
    {
      const float kInvPhi = 0.6180339887f;        // 1/φ
      const float u = ((float)t * kInvPhi + 0.5f);
      const float uFrac = u - (float)((int)u);   // fmod to [0, 1)
      const float zHat = 2.0f * uFrac - 1.0f;
      const float r = sqrtf(1.0f - zHat * zHat);
      const float rx = mpNetwork->getReflectorX(t);
      const float ry = mpNetwork->getReflectorY(t);
      const float phi = atan2f(ry, rx);
      const float radial = 1.0f + zGlitch * 0.3f;
      *sx = r * cosf(phi) * radial;
      *sy = zHat          * radial;
      *sz = r * sinf(phi) * radial;

      // Motion-driven per-tap perturbation.
      const float motionNorm = mpNetwork->getMotionNorm();
      if (motionNorm > 0.0f)
      {
        uint32_t hp = mPerturbSeed ^ ((uint32_t)t * 2654435761u);
        hp = hp * 1103515245u + 12345u;
        const float ox =
          ((float)((hp >> 16) & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f);
        hp = hp * 1103515245u + 12345u;
        const float oy =
          ((float)((hp >> 16) & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f);
        hp = hp * 1103515245u + 12345u;
        const float oz =
          ((float)((hp >> 16) & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f);
        const float kPerturbMag = 0.6f;
        const float scale = motionNorm * kPerturbMag;
        *sx += ox * scale;
        *sy += oy * scale;
        *sz += oz * scale;
      }
    }

    // Project a 3D sphere point through view rotation (mRotAngle
    // + listener-motion sphere-rotation) and X tilt to a 2D pixel.
    // Returns view-z depth via *outDepth (front-of-sphere positive).
    inline void projectSphere(float sx, float sy, float sz,
                              float sphereRot, float scale,
                              int *outPx, int *outPy, float *outDepth) const
    {
      // Sphere rotation around Y axis (combined view tumble +
      // listener-motion spin).
      const float cosA = cosf(mRotAngle + sphereRot);
      const float sinA = sinf(mRotAngle + sphereRot);
      const float rx    =  sx * cosA + sz * sinA;
      const float rzNew = -sx * sinA + sz * cosA;
      const float ry    =  sy;
      // X-axis tilt for 2.5D depth feel.
      const float costilt = 0.9553f;
      const float sintilt = 0.2955f;
      const float fx = rx;
      const float fy = ry * costilt - rzNew * sintilt;
      const float depth = rzNew * costilt + ry * sintilt;
      const int w = mWidth < kMaxW ? mWidth : kMaxW;
      const int h = mHeight < kMaxH ? mHeight : kMaxH;
      *outPx = (int)(fx * scale) + w / 2;
      *outPy = (int)(fy * scale) + h / 2;
      *outDepth = depth;
    }

    // Simple parametric line draw.
    inline void drawLine(od::FrameBuffer &fb,
                         int x0, int y0, int x1, int y1,
                         int brightness) const
    {
      const int w = mWidth < kMaxW ? mWidth : kMaxW;
      const int h = mHeight < kMaxH ? mHeight : kMaxH;
      const int dx = x1 - x0;
      const int dy = y1 - y0;
      const int adx = dx < 0 ? -dx : dx;
      const int ady = dy < 0 ? -dy : dy;
      const int steps = adx > ady ? adx : ady;
      if (steps == 0) return;
      const float invSteps = 1.0f / (float)steps;
      for (int s = 0; s <= steps; s++)
      {
        const float u = (float)s * invSteps;
        const int x = x0 + (int)((float)dx * u);
        const int y = y0 + (int)((float)dy * u);
        if (x >= 0 && x < w && y >= 0 && y < h)
          fb.pixel(brightness, mWorldLeft + x, mWorldBottom + y);
      }
    }

    inline float tapZ(int t) const
    {
      const int mode = mpNetwork->getTapMode(t);
      switch (mode)
      {
        case NETWORK_TAP_NORMAL: return 0.0f;
        // MUTE and REVERSE use small radial offsets — mode is
        // communicated by brightness (smoothed) more than position
        // so the mode-mutex shuffle at walker wrap doesn't visibly
        // collapse many taps toward sphere center simultaneously.
        case NETWORK_TAP_MUTE:   return -0.3f;
        case NETWORK_TAP_STUTTER:
          // No radial movement — stutter taps stay on the sphere
          // surface. Visual character comes from the brightness
          // flicker (flash at each iteration start) plus the ghost
          // trail in the persistence buffer.
          return 0.0f;
        case NETWORK_TAP_CRUSH:
        {
          uint32_t h = (uint32_t)t * 2654435761u +
                       (uint32_t)mFrameCounter;
          h = h * 1103515245u + 12345u;
          const float n =
            ((float)((h >> 16) & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f);
          return 0.3f * n;
        }
        case NETWORK_TAP_SCRUB:
        {
          const int offset = mpNetwork->getTapScrubOffset(t);
          const int maxD = mpNetwork->getMaxDelayInSamples();
          if (maxD <= 0) return 0.0f;
          const float scrubMaxSamples = 0.25f * (float)maxD;
          if (scrubMaxSamples < 1.0f) return 0.0f;
          float norm = (float)offset / scrubMaxSamples;
          if (norm > 1.0f) norm = 1.0f;
          if (norm < -1.0f) norm = -1.0f;
          return 0.4f * norm;
        }
        case NETWORK_TAP_REVERSE: return -0.1f;
      }
      return 0.0f;
    }

  public:
    // BISECTION DIAGNOSTIC. Re-enable subsystems one at a time
    // to isolate which one produces the dim+respawn-pull-in cycle.
    //   0 = full draw path (production behavior)
    //   1 = minimal: 32 static dots + listener marker (no motion)
    //   2 = + passive mRotAngle rotation
    //   3 = + persistence buffer (fade + trail deposit)
    //   4 = + tapZ smoothing
    //   5 = + brightness smoothing + depth dim
    //   6 = + ping rings (cross-detection + render)
    //   7 = + shell + listener trace + ricochet/ping-flash
    // Stage 1 confirmed clean — bug is in draw path at stage > 1.
    // Bisection complete. Set to 0 for production (full draw path).
    // Non-zero values re-engage the bisection scaffolding kept
    // below for future diagnostic use; the journey to motion-
    // driven sphereRot as the cause is in commit history
    // (2.6.1.39..54).
    static const int kBisectStage = 0;

    virtual void draw(od::FrameBuffer &fb)
    {
      const int w = mWidth < kMaxW ? mWidth : kMaxW;
      const int h = mHeight < kMaxH ? mHeight : kMaxH;

      if (!mpNetwork)
      {
        fb.fill(BLACK, mWorldLeft, mWorldBottom,
                mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);
        return;
      }

      // BISECTION early-return — runs at stages 1..7 (anything
      // except 0 = full draw path).
      if (kBisectStage >= 1)
      {
        // Stage 14+: sphereRad from sizeNorm, sphereRot from
        // motion-driven accumulator, mFrameCounter increments
        // each frame, decayMapped from decayNorm.
        const float kBaseScale = 26.0f;
        const float kMinSphereRad = 0.29f * kBaseScale;
        const float kBisectSphereRad = (kBisectStage >= 14)
          ? (kMinSphereRad +
             mpNetwork->getSizeNorm() * (kBaseScale - kMinSphereRad))
          : 16.0f;
        const float kInvPhi = 0.6180339887f;
        const float kGoldenAngle = 2.39996323f;
        const int kBisectActiveTaps =
          (kBisectStage >= 13) ? mpNetwork->getActiveTapCount() : 32;

        if (kBisectStage >= 14)
        {
          mFrameCounter++;
        }

        // Sphere rotation accumulator advance.
        // SUB-BISECT 14a: motion-driven sphereRot DISABLED — was
        // added at stage 14 along with size/decay/mFrameCounter.
        // Stage 14 reproduced the zoom-out+reset symptoms; this
        // variant removes the motion mapping piece to test if
        // that single addition is the cause.
        float bisectSphereRot = 0.0f;
        // if (kBisectStage >= 14) {  ... }  // disabled

        // Stage 2+: passive sphere rotation.
        if (kBisectStage >= 2)
        {
          mRotAngle += 0.01f;
          if (mRotAngle > 6.2832f) mRotAngle -= 6.2832f;
        }

        // Decay-mapped constants (stage 3+). Constant decay so no
        // parameter influence — only the persistence subsystem
        // mechanics are tested.
        float decayMapped = 0.0f;
        if (kBisectStage >= 3)
        {
          // Stage 14+: real decayNorm; earlier stages: constant 0.5.
          const float kBisectDecay =
            (kBisectStage >= 14) ? mpNetwork->getDecayNorm() : 0.5f;
          float dm = kBisectDecay * (1.0f / 0.6f);
          if (dm > 1.0f) dm = 1.0f;
          decayMapped = dm * dm * dm;
        }

        // Stage 7+: tapZ smoothing (reads tap mode from Network).
        // At glitch=0 all taps NORMAL → tapZ=0 → smoothed=0 →
        // radial=1.0 (no animation). Tests whether tap mode read
        // path or radial smoothing introduces artifacts.
        if (kBisectStage >= 7)
        {
          for (int t = 0; t < kMaxNetworkTaps; t++)
          {
            const float target = tapZ(t);
            if (mTapZSmoothed[t] < -1.5f)
              mTapZSmoothed[t] = target;
            else
              mTapZSmoothed[t] += (target - mTapZSmoothed[t]) * 0.3f;
          }
        }

        // Stage 3+: persistence buffer fade.
        if (kBisectStage >= 3)
        {
          const int kFadeInterval = 1 + (int)(decayMapped * 5.0f);
          mPersistFadeCounter++;
          if (mPersistFadeCounter >= kFadeInterval)
          {
            mPersistFadeCounter = 0;
            for (int i = 0; i < w * h; i++)
            {
              if (mPixels[i] > 0) mPixels[i]--;
            }
          }
        }

        // Stage 12+: stutter loop iteration → ghost deposit +
        // flicker counter reset. At glitch=0 no STUTTER taps so
        // curIter is always 0, iterStart never true, no ghost
        // spawn. mStutterFlicker increments harmlessly.
        if (kBisectStage >= 12)
        {
          for (int t = 0; t < kBisectActiveTaps; t++)
          {
            const int curIter = mpNetwork->getTapStutterIter(t);
            const int lastIter = (int)mLastStutterIter[t];
            const bool iterStart = (curIter > 0 && curIter < lastIter);
            if (iterStart)
            {
              mStutterFlicker[t] = 0;
              const float us = ((float)t * kInvPhi + 0.5f);
              const float uFracs = us - (float)((int)us);
              const float zHats = 2.0f * uFracs - 1.0f;
              const float rxys = sqrtf(1.0f - zHats * zHats);
              float phis;
              if (kBisectStage >= 6)
              {
                const float rxx = mpNetwork->getReflectorX(t);
                const float ryy = mpNetwork->getReflectorY(t);
                phis = atan2f(ryy, rxx);
              }
              else
              {
                phis = (float)t * kGoldenAngle;
              }
              const float radials =
                (kBisectStage >= 7) ? (1.0f + mTapZSmoothed[t] * 0.3f) : 1.0f;
              const float gsx = rxys * cosf(phis) * radials;
              const float gsy = zHats * radials;
              const float gsz = rxys * sinf(phis) * radials;
              uint32_t hg = (uint32_t)t * 2654435761u +
                            (uint32_t)curIter;
              hg = hg * 1103515245u + 12345u;
              const float jitter =
                ((float)((hg >> 16) & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f) * 0.15f;
              int gpx, gpy;
              float gpz;
              projectSphere(gsx + jitter, gsy, gsz, bisectSphereRot,
                            kBisectSphereRad, &gpx, &gpy, &gpz);
              if (gpx >= 0 && gpx < w && gpy >= 0 && gpy < h)
              {
                mPixels[gpy * w + gpx] = 12;
              }
            }
            else
            {
              if (mStutterFlicker[t] < 250) mStutterFlicker[t]++;
            }
            mLastStutterIter[t] = (uint8_t)curIter;
          }
        }

        // Stage 3+: per-tap trail deposit into persistence (only
        // taps that pass the deterministic eligibility hash,
        // gated by decayMapped × 0.4 = ~40% at full decay).
        if (kBisectStage >= 3)
        {
          const float trailThreshold = decayMapped * 0.4f;
          for (int t = 0; t < kBisectActiveTaps; t++)
          {
            uint32_t hT = (uint32_t)t * 2654435761u + 0xBEEFCAFEu;
            hT = hT * 1103515245u + 12345u;
            const float trailHash =
              (float)((hT >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
            if (trailHash > trailThreshold) continue;

            const float u = ((float)t * kInvPhi + 0.5f);
            const float uFrac = u - (float)((int)u);
            const float zHat = 2.0f * uFrac - 1.0f;
            const float rxy = sqrtf(1.0f - zHat * zHat);
            float phi;
            if (kBisectStage >= 6)
            {
              const float rx = mpNetwork->getReflectorX(t);
              const float ry = mpNetwork->getReflectorY(t);
              phi = atan2f(ry, rx);
            }
            else
            {
              phi = (float)t * kGoldenAngle;
            }
            const float radial =
              (kBisectStage >= 7) ? (1.0f + mTapZSmoothed[t] * 0.3f) : 1.0f;
            const float sx = rxy * cosf(phi) * radial;
            const float sy = zHat * radial;
            const float sz = rxy * sinf(phi) * radial;

            int px, py;
            float pz;
            projectSphere(sx, sy, sz, bisectSphereRot, kBisectSphereRad,
                          &px, &py, &pz);
            if (px < 0 || px >= w || py < 0 || py >= h) continue;
            int b = (int)mPixels[py * w + px] + 5;
            if (b > 11) b = 11;
            mPixels[py * w + px] = (uint8_t)b;
          }
        }

        fb.fill(BLACK, mWorldLeft, mWorldBottom,
                mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);

        // Stage 3+: render persistence buffer.
        if (kBisectStage >= 3)
        {
          for (int y = 0; y < h; y++)
          {
            for (int x = 0; x < w; x++)
            {
              const uint8_t v = mPixels[y * w + x];
              if (v > 0)
                fb.pixel(v, mWorldLeft + x, mWorldBottom + y);
            }
          }
        }

        // Stage 11+: sonar pings — spawn (motion-rate accumulator),
        // cross detection (writes mTapPingFlash[t]), and ring
        // render. At motion=0, mPingPhase doesn't advance →
        // no spawns. Cross detection loop iterates over active
        // pings only (empty at steady motion=0). Should be no-op
        // visually at motion=0; only matters once motion>0.
        if (kBisectStage >= 11)
        {
          // mTapPingFlash decrement.
          for (int t = 0; t < kMaxNetworkTaps; t++)
          {
            if (mTapPingFlash[t] > 0) mTapPingFlash[t]--;
          }

          const float motionNorm2 = mpNetwork->getMotionNorm();
          const float kPingHzAtFullMotion = 0.25f;
          const float kAssumedFrameRateHz = 60.0f;
          const float perFrameAdvance =
            motionNorm2 * (kPingHzAtFullMotion / kAssumedFrameRateHz);
          mPingPhase += perFrameAdvance;
          if (mPingPhase >= 1.0f)
          {
            mPingPhase -= 1.0f;
            if (mPingCount < kMaxPings)
            {
              mPings[mPingCount].age = 0;
              mPingCount++;
            }
          }

          const float motionScale2 = 0.1f + motionNorm2 * 0.9f;
          const float kPingSpeedDist =
            (2.6f / (float)kPingMaxAge) * motionScale2;

          // Listener world position for cross detection.
          const float listenerPhase2 = mpNetwork->getListenerPhase();
          const float twoPi2 = 2.0f * 3.14159265f;
          const float kListenerR = 1.3f;
          const float listenerX =
            cosf(twoPi2 * listenerPhase2) * kListenerR;
          const float listenerY =
            sinf(twoPi2 * listenerPhase2) * kListenerR;

          // Cross detection.
          for (int p = 0; p < mPingCount; )
          {
            Ping *ping = &mPings[p];
            const float r3D     = (float)ping->age       * kPingSpeedDist;
            const float r3DPrev = (float)(ping->age - 1) * kPingSpeedDist;

            for (int t = 0; t < kBisectActiveTaps; t++)
            {
              if (mpNetwork->getTapMode(t) == NETWORK_TAP_MUTE) continue;
              const float dx = mpNetwork->getReflectorX(t) - listenerX;
              const float dy = mpNetwork->getReflectorY(t) - listenerY;
              const float dist = sqrtf(dx * dx + dy * dy);
              if (dist > r3DPrev && dist <= r3D)
              {
                uint8_t flashLen = kPingFlashFrames;
                if (mpNetwork->getTapMode(t) == NETWORK_TAP_STUTTER)
                  flashLen = kPingFlashFrames * 2;
                mTapPingFlash[t] = flashLen;
              }
            }

            ping->age++;
            if (ping->age >= kPingMaxAge)
            {
              mPings[p] = mPings[mPingCount - 1];
              mPingCount--;
            }
            else
            {
              p++;
            }
          }

          // Ring render — full original .36 extent (rings expand
          // freely until off-screen). Bisection ruled out ring
          // size as the cause of the zoom-cycle artifact, so the
          // .37 visibility cap is removed.
          const int viewCxR = w / 2;
          const int viewCyR = h / 2;
          for (int p = 0; p < mPingCount; p++)
          {
            const Ping *ping = &mPings[p];
            const float r3D = (float)ping->age * kPingSpeedDist;
            const float screenR = r3D * kBisectSphereRad;
            if (screenR < 0.5f || screenR > (float)(w + h))
              continue;
            const int brightness = 7 - (7 * ping->age) / kPingMaxAge;
            if (brightness <= 0) continue;
            const int steps = (int)(6.2832f * screenR + 8.0f);
            const float invSteps = 1.0f / (float)steps;
            for (int s = 0; s < steps; s++)
            {
              const float a = (float)s * invSteps * 6.2832f;
              const int rpx = viewCxR + (int)(screenR * cosf(a));
              const int rpy = viewCyR + (int)(screenR * sinf(a));
              if (rpx >= 0 && rpx < w && rpy >= 0 && rpy < h)
              {
                fb.pixel(brightness, mWorldLeft + rpx, mWorldBottom + rpy);
              }
            }
          }
        }

        // Stage 10+: connectivity shell — update mShellLevel for
        // each tap, build front-of-sphere node list, draw k-NN
        // edges between them. mShellLevel sentinel snaps on first
        // use to defend against any reset. At glitch=0 with
        // connectivity>0, fb-set is stable (deterministic from
        // params), so shellLevel converges to 1.0 for fb taps and
        // 0.0 for others — stable lines, no animation expected.
        if (kBisectStage >= 10)
        {
          const float kShFadeIn  = 1.0f / 12.0f;
          const float kShFadeOut = 1.0f / 18.0f;
          for (int t = 0; t < kMaxNetworkTaps; t++)
          {
            const bool isFb =
              (t < kBisectActiveTaps) &&
              (mpNetwork->getFbWeight(t) != 0.0f);
            if (mShellLevel[t] < 0.0f)
            {
              mShellLevel[t] = isFb ? 1.0f : 0.0f;
            }
            else if (isFb)
            {
              mShellLevel[t] += kShFadeIn;
              if (mShellLevel[t] > 1.0f) mShellLevel[t] = 1.0f;
            }
            else
            {
              mShellLevel[t] -= kShFadeOut;
              if (mShellLevel[t] < 0.0f) mShellLevel[t] = 0.0f;
            }
          }

          int shellPx[kMaxNetworkTaps];
          int shellPy[kMaxNetworkTaps];
          int shellT [kMaxNetworkTaps];
          int shellCount = 0;
          for (int t = 0; t < kBisectActiveTaps; t++)
          {
            if (mShellLevel[t] < 0.05f) continue;

            const float u2 = ((float)t * kInvPhi + 0.5f);
            const float uFrac2 = u2 - (float)((int)u2);
            const float zHat2 = 2.0f * uFrac2 - 1.0f;
            const float rxy2 = sqrtf(1.0f - zHat2 * zHat2);
            float phi2;
            if (kBisectStage >= 6)
            {
              const float rx = mpNetwork->getReflectorX(t);
              const float ry = mpNetwork->getReflectorY(t);
              phi2 = atan2f(ry, rx);
            }
            else
            {
              phi2 = (float)t * kGoldenAngle;
            }
            const float radial2 =
              (kBisectStage >= 7) ? (1.0f + mTapZSmoothed[t] * 0.3f) : 1.0f;
            const float ssx = rxy2 * cosf(phi2) * radial2;
            const float ssy = zHat2 * radial2;
            const float ssz = rxy2 * sinf(phi2) * radial2;

            int spx, spy;
            float spz;
            projectSphere(ssx, ssy, ssz, bisectSphereRot, kBisectSphereRad,
                          &spx, &spy, &spz);
            if (spx < 0 || spx >= w || spy < 0 || spy >= h) continue;
            shellPx[shellCount] = spx;
            shellPy[shellCount] = spy;
            shellT [shellCount] = t;
            shellCount++;
          }

          for (int i = 0; i < shellCount; i++)
          {
            int n1 = -1, n2 = -1;
            long d1 = 1L << 30, d2 = 1L << 30;
            for (int j = 0; j < shellCount; j++)
            {
              if (i == j) continue;
              const long dxL = shellPx[i] - shellPx[j];
              const long dyL = shellPy[i] - shellPy[j];
              const long d = dxL * dxL + dyL * dyL;
              if (d < d1) { n2 = n1; d2 = d1; n1 = j; d1 = d; }
              else if (d < d2) { n2 = j; d2 = d; }
            }
            const float lvlI = mShellLevel[shellT[i]];
            if (n1 >= 0 && i < n1)
            {
              const float gm = sqrtf(lvlI * mShellLevel[shellT[n1]]);
              int b = 1 + (int)(gm * 5.5f + 0.5f);
              if (b > 6) b = 6;
              if (b >= 1)
                drawLine(fb, shellPx[i], shellPy[i],
                         shellPx[n1], shellPy[n1], b);
            }
            if (n2 >= 0 && i < n2)
            {
              const float gm = sqrtf(lvlI * mShellLevel[shellT[n2]]);
              int b = 1 + (int)(gm * 5.5f + 0.5f);
              if (b > 6) b = 6;
              if (b >= 1)
                drawLine(fb, shellPx[i], shellPy[i],
                         shellPx[n2], shellPy[n2], b);
            }
          }
        }

        // Stage 9+: listener trace render. 128 dots at sphere
        // equator at past walker phase longitudes. At motion=0
        // all values are the same (current mWalkerPos) so all
        // dots collapse to a single pixel.
        if (kBisectStage >= 9)
        {
          const int traceSize = mpNetwork->getListenerTraceSize();
          const float twoPi = 2.0f * 3.14159265f;
          for (int i = 0; i < traceSize; i++)
          {
            const float oldPhase = mpNetwork->getListenerTracePhase(i);
            const float phiTrace = twoPi * oldPhase;
            const float lsx = cosf(phiTrace);
            const float lsy = 0.0f;
            const float lsz = sinf(phiTrace);
            int lpx, lpy;
            float lpz;
            projectSphere(lsx, lsy, lsz, bisectSphereRot, kBisectSphereRad,
                          &lpx, &lpy, &lpz);
            if (lpx < 0 || lpx >= w || lpy < 0 || lpy >= h) continue;
            int tb = 3 + (8 * i) / traceSize;
            if (tb > 11) tb = 11;
            fb.pixel(tb, mWorldLeft + lpx, mWorldBottom + lpy);
          }
        }

        // Stage 8+: wetBoost from output ring (audio-coupled
        // brightness target for NORMAL taps).
        int wetBoost = 0;
        if (kBisectStage >= 8)
        {
          const int ringSize = mpNetwork->getOutputRingSize();
          float wetLvlSq = 0.0f;
          for (int i = ringSize - 16; i < ringSize; i++)
          {
            const float s = mpNetwork->getOutputSample(i);
            if (!(s == s)) continue;
            wetLvlSq += s * s;
          }
          const float wetLvl = sqrtf(wetLvlSq * (1.0f / 16.0f));
          wetBoost = (int)(wetLvl * 8.0f);
          if (wetBoost > 4) wetBoost = 4;
        }

        // Tap dots (all bisect stages).
        for (int t = 0; t < kBisectActiveTaps; t++)
        {
          const float u = ((float)t * kInvPhi + 0.5f);
          const float uFrac = u - (float)((int)u);
          const float zHat = 2.0f * uFrac - 1.0f;
          const float rxy = sqrtf(1.0f - zHat * zHat);
          const float phi = (float)t * kGoldenAngle;
          const float sx = rxy * cosf(phi);
          const float sy = zHat;
          const float sz = rxy * sinf(phi);

          int px, py;
          float pz;
          projectSphere(sx, sy, sz, bisectSphereRot, kBisectSphereRad,
                        &px, &py, &pz);
          if (px < 0 || px >= w || py < 0 || py >= h) continue;

          // Stage 8+: mode-dependent brightness target.
          // At glitch=0 all taps NORMAL → target = 9 + wetBoost.
          int targetBrightness = 9;
          if (kBisectStage >= 8)
          {
            const int mode = mpNetwork->getTapMode(t);
            switch (mode)
            {
              case NETWORK_TAP_MUTE:    targetBrightness = 3; break;
              case NETWORK_TAP_CRUSH:
              {
                uint32_t hh = (uint32_t)t * 2654435761u +
                              (uint32_t)mFrameCounter;
                hh = hh * 1103515245u + 12345u;
                targetBrightness = ((hh >> 16) & 1u) ? 13 : 7;
                break;
              }
              case NETWORK_TAP_REVERSE: targetBrightness = 11; break;
              case NETWORK_TAP_NORMAL:  targetBrightness = 9 + wetBoost; break;
              case NETWORK_TAP_STUTTER:
              {
                const int ff = (int)mStutterFlicker[t];
                targetBrightness = (ff < 2) ? 15 : 8;
                break;
              }
              case NETWORK_TAP_SCRUB:
              default:                  targetBrightness = 12; break;
            }
          }

          // Stage 4+: brightness smoothing.
          int brightness = 9;
          if (kBisectStage >= 4)
          {
            if (mTapBrightSmoothed[t] < 0.0f)
            {
              mTapBrightSmoothed[t] = (float)targetBrightness;
              brightness = targetBrightness;
            }
            else
            {
              mTapBrightSmoothed[t] +=
                ((float)targetBrightness - mTapBrightSmoothed[t]) * 0.25f;
              brightness = (int)(mTapBrightSmoothed[t] + 0.5f);
            }
          }
          // Stage 5+: back-face depth dim (geometric, no params).
          if (kBisectStage >= 5 && pz < 0.0f)
          {
            const int dim = (int)((-pz) * 1.0f + 0.5f);
            brightness -= dim;
            if (brightness < 3) brightness = 3;
          }
          // Stage 8+: ricochet flash + ping flash brightness.
          if (kBisectStage >= 8)
          {
            const int flashMax = mpNetwork->getRicochetFlashMax();
            const int rflash = mpNetwork->getRicochetFlash(t);
            if (rflash > 0 && flashMax > 0)
              brightness += (rflash * 5) / flashMax;
            const int pflash = (int)mTapPingFlash[t];
            if (pflash > 0)
              brightness += (pflash * 6) / kPingFlashFrames;
            if (brightness > 15) brightness = 15;
            if (brightness < 0)  brightness = 0;
          }
          fb.pixel(brightness, mWorldLeft + px, mWorldBottom + py);
        }

        const int viewCx = w / 2;
        const int viewCy = h / 2;
        if (viewCx >= 1 && viewCx < w - 1 &&
            viewCy >= 1 && viewCy < h - 1)
        {
          fb.pixel(WHITE, mWorldLeft + viewCx,     mWorldBottom + viewCy);
          fb.pixel(11,    mWorldLeft + viewCx + 1, mWorldBottom + viewCy);
          fb.pixel(11,    mWorldLeft + viewCx - 1, mWorldBottom + viewCy);
          fb.pixel(11,    mWorldLeft + viewCx,     mWorldBottom + viewCy + 1);
          fb.pixel(11,    mWorldLeft + viewCx,     mWorldBottom + viewCy - 1);
        }
        return;
      }

      mFrameCounter++;

      const float sizeNorm = mpNetwork->getSizeNorm();
      const float kBaseScale = 26.0f;
      // Floor: size=0 maps to what size=0.29 used to look like, so
      // the sphere never collapses to a microscopic dot at low size.
      // Top end (size=1) unchanged.
      const float kMinSphereRad = 0.29f * kBaseScale;
      const float sphereRad =
        kMinSphereRad + sizeNorm * (kBaseScale - kMinSphereRad);

      // Listener world position (driven by walker phase — used only
      // for sonar-ring time-of-flight cross detection where the
      // walker's actual position matters; the listener trace also
      // reads walker history for trace dot longitudes).
      const float listenerPhase = mpNetwork->getListenerPhase();
      const float kListenerR = 1.3f;
      const float twoPi = 2.0f * 3.14159265f;
      const float cosM = cosf(twoPi * listenerPhase);
      const float sinM = sinf(twoPi * listenerPhase);
      const float listenerX = cosM * kListenerR;
      const float listenerY = sinM * kListenerR;

      // Sphere rotation: passive tumble only (via mRotAngle,
      // advanced below). Motion-driven sphereRot was removed
      // after a 14-stage subsystem bisection identified it as
      // the cause of the "very short dim → respawn at edges,
      // pull in toward center, repeat" cycle. The mechanism:
      // at non-zero motion the motion-driven advance combined
      // with passive rotation produces a fast cyclic completion
      // (and at motion≈0.38 the two cancel; beyond that, the
      // sphere reverses direction). Motion feedback is still
      // visible elsewhere (ping cadence, ring expansion speed,
      // listener trace dot longitudes).
      const float motionNorm = mpNetwork->getMotionNorm();
      const float sphereRot = 0.0f;

      // Wet-level pulse from output ring.
      const int ringSize = mpNetwork->getOutputRingSize();
      float wetLvlSq = 0.0f;
      for (int i = ringSize - 16; i < ringSize; i++)
      {
        const float s = mpNetwork->getOutputSample(i);
        if (!(s == s)) continue;
        wetLvlSq += s * s;
      }
      const float wetLvl = sqrtf(wetLvlSq * (1.0f / 16.0f));
      int wetBoost = (int)(wetLvl * 8.0f);
      if (wetBoost > 4) wetBoost = 4;

      // ---- 1. Fade persistence buffer ----
      // Fade interval scales with decay via a cubic curve that
      // saturates at decay = 0.6: little visual trail effect at
      // low decay (where the audible feedback is also subtle),
      // ramping up exponentially as decay approaches 0.6, then
      // clamped at maximum trail length for any higher decay.
      // Per-tap deposit cadence is unchanged so stationary taps
      // still reach steady-state brightness fast.
      const float decayNorm = mpNetwork->getDecayNorm();
      float dm = decayNorm * (1.0f / 0.6f);
      if (dm > 1.0f) dm = 1.0f;
      const float decayMapped = dm * dm * dm;
      const int kFadeInterval = 1 + (int)(decayMapped * 5.0f);
      mPersistFadeCounter++;
      if (mPersistFadeCounter >= kFadeInterval)
      {
        mPersistFadeCounter = 0;
        for (int i = 0; i < w * h; i++)
        {
          if (mPixels[i] > 0) mPixels[i]--;
        }
      }

      // ---- 1b. Smooth tapZ for radial scaling ----
      // Per-tap viz-side LP toward raw tapZ() target. ~50ms time
      // constant at 60fps (alpha = 0.3 → ~3 frames to converge).
      // Mode reshuffles at walker wrap ramp visually instead of
      // popping. STUTTER's stutter-loop sine pulse comes through
      // with mild attenuation; full pulse character preserved.
      // First use after constructor: snap to target (defends
      // against framework graphic re-instantiation).
      for (int t = 0; t < kMaxNetworkTaps; t++)
      {
        const float target = tapZ(t);
        if (mTapZSmoothed[t] < -1.5f)
          mTapZSmoothed[t] = target;
        else
          mTapZSmoothed[t] += (target - mTapZSmoothed[t]) * 0.3f;
      }

      // ---- 2. Stutter iteration → ghost spawn + flicker reset ----
      // On every stutter loop iteration start (curIter decremented):
      //   - reset the per-tap flicker counter so the tap dot flashes
      //     bright this frame and the next, then fades to the dim base.
      //   - deposit a brightness-12 ghost into the persistence buffer
      //     at the tap's current sphere position. As the sphere
      //     rotates with listener motion, successive ghosts spread
      //     into a dotted trail behind the tap. The trail fades at
      //     the global persistence rate (decay-scaled above).
      const int activeCount = mpNetwork->getActiveTapCount();
      for (int t = 0; t < activeCount; t++)
      {
        const int curIter = mpNetwork->getTapStutterIter(t);
        const int lastIter = (int)mLastStutterIter[t];
        const bool iterStart = (curIter > 0 && curIter < lastIter);
        if (iterStart)
        {
          mStutterFlicker[t] = 0;
          float sx, sy, sz;
          tapToSphere(t, mTapZSmoothed[t], &sx, &sy, &sz);
          // Small sphere-tangent jitter.
          uint32_t hg = (uint32_t)t * 2654435761u +
                        (uint32_t)curIter;
          hg = hg * 1103515245u + 12345u;
          const float jitter =
            ((float)((hg >> 16) & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f) * 0.15f;
          int px, py;
          float pz;
          projectSphere(sx + jitter, sy, sz,
                        sphereRot, sphereRad,
                        &px, &py, &pz);
          if (px >= 0 && px < w && py >= 0 && py < h)
          {
            mPixels[py * w + px] = 12;
          }
        }
        else
        {
          if (mStutterFlicker[t] < 250) mStutterFlicker[t]++;
        }
        mLastStutterIter[t] = (uint8_t)curIter;
      }

      // ---- 3. Sonar pings ----
      for (int t = 0; t < kMaxNetworkTaps; t++)
      {
        if (mTapPingFlash[t] > 0) mTapPingFlash[t]--;
      }

      // Ping spawn: constant-rate clock with rate ∝ motion.
      // At motion=1, one ping every 4s (matches the walker's
      // base rate of 0.25Hz). At motion=0.1, one every 40s. At
      // motion=0, the clock freezes — existing pings finish
      // their lifecycle, no new ones spawn. Independent of the
      // walker's smoothed-random instantaneous velocity so the
      // cadence is perceptually steady at any fixed motion.
      {
        const float kPingHzAtFullMotion = 0.25f;
        const float kAssumedFrameRateHz = 60.0f;
        const float perFrameAdvance =
          motionNorm * (kPingHzAtFullMotion / kAssumedFrameRateHz);
        mPingPhase += perFrameAdvance;
        if (mPingPhase >= 1.0f)
        {
          mPingPhase -= 1.0f;
          if (mPingCount < kMaxPings)
          {
            // Per-ping axis: Fibonacci-sphere position from spawn
            // counter. Each successive ping gets a deterministic
            // but well-spread orientation.
            const float idF = (float)mPingSpawnCount;
            const float kInvPhiP = 0.6180339887f;
            const float uPing = (idF * kInvPhiP + 0.5f);
            const float uFracP = uPing - (float)((int)uPing);
            const float yA = 2.0f * uFracP - 1.0f;
            const float rA = sqrtf(1.0f - yA * yA);
            const float kGAP = 2.39996323f;
            const float phiA = idF * kGAP;
            mPings[mPingCount].age = 0;
            mPings[mPingCount].axisX = rA * cosf(phiA);
            mPings[mPingCount].axisY = yA;
            mPings[mPingCount].axisZ = rA * sinf(phiA);
            mPingCount++;
            mPingSpawnCount++;
          }
        }
      }

      // Ping radial expansion speed — motion-independent. Every
      // spawned ping reaches max r3D (2.6) by age kPingMaxAge
      // regardless of motion level. Motion now affects only
      // spawn cadence; once a ping exists it always expands fully.
      const float kPingSpeedDist = 2.6f / (float)kPingMaxAge;
      for (int p = 0; p < mPingCount; )
      {
        Ping *ping = &mPings[p];
        const float r3D     = (float)ping->age       * kPingSpeedDist;
        const float r3DPrev = (float)(ping->age - 1) * kPingSpeedDist;

        // Reflector cross detection uses ACTUAL world distance
        // from listener (preserved separately from sphere viz pos).
        for (int t = 0; t < activeCount; t++)
        {
          if (mpNetwork->getTapMode(t) == NETWORK_TAP_MUTE) continue;
          const float dx = mpNetwork->getReflectorX(t) - listenerX;
          const float dy = mpNetwork->getReflectorY(t) - listenerY;
          const float dist = sqrtf(dx * dx + dy * dy);
          if (dist > r3DPrev && dist <= r3D)
          {
            uint8_t flashLen = kPingFlashFrames;
            if (mpNetwork->getTapMode(t) == NETWORK_TAP_STUTTER)
              flashLen = kPingFlashFrames * 2;
            mTapPingFlash[t] = flashLen;
          }
        }

        ping->age++;
        if (ping->age >= kPingMaxAge)
        {
          mPings[p] = mPings[mPingCount - 1];
          mPingCount--;
        }
        else
        {
          p++;
        }
      }

      // ---- 4. Advance rotation ----
      mRotAngle += 0.01f;
      if (mRotAngle > 6.2832f) mRotAngle -= 6.2832f;

      // ---- 5. Per-tap trail deposit into persistence ----
      // Each frame, a SUBSET of taps deposits a low-bright pixel
      // at its current sphere position, building a fading arc as
      // the sphere rotates. Eligibility is gated by a per-tap
      // hash so only ~40% of taps get tails at full decay (full
      // decay = sphere too messy if every dot trails). At lower
      // decay the fraction scales with decayMapped, so trails are
      // rare at low decay and common (capped at 40%) at high.
      // MUTE skipped (no signal to trail). STUTTER skipped — their
      // designated flicker + ghost animation is the trail; layering
      // a second trail on top obscures it.
      const float kTrailMaxFraction = 0.4f;
      const float trailThreshold = decayMapped * kTrailMaxFraction;
      for (int t = 0; t < activeCount; t++)
      {
        const int mode = mpNetwork->getTapMode(t);
        if (mode == NETWORK_TAP_MUTE)    continue;
        if (mode == NETWORK_TAP_STUTTER) continue;
        // Per-tap deterministic eligibility hash (stable across
        // frames so trails belong to a consistent subset).
        uint32_t hT = (uint32_t)t * 2654435761u + 0xBEEFCAFEu;
        hT = hT * 1103515245u + 12345u;
        const float trailHash =
          (float)((hT >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
        if (trailHash > trailThreshold) continue;

        float sx, sy, sz;
        tapToSphere(t, mTapZSmoothed[t], &sx, &sy, &sz);
        // REVERSE taps orbit opposite the sphere's rotation: apply
        // a pre-rotation of -2 × (mRotAngle + sphereRot) so that
        // projectSphere's +(mRotAngle + sphereRot) results in a
        // NET rotation of -(mRotAngle + sphereRot). Trail deposits
        // follow the reversed motion so trails belong to the tap.
        if (mode == NETWORK_TAP_REVERSE)
        {
          const float ar = -2.0f * (mRotAngle + sphereRot);
          const float cr = cosf(ar);
          const float sr = sinf(ar);
          const float nsx = sx * cr + sz * sr;
          const float nsz = -sx * sr + sz * cr;
          sx = nsx; sz = nsz;
        }
        int px, py;
        float pz;
        projectSphere(sx, sy, sz, sphereRot, sphereRad,
                      &px, &py, &pz);
        if (px < 0 || px >= w || py < 0 || py >= h) continue;
        int b = (int)mPixels[py * w + px] + 5;
        if (b > 11) b = 11;
        mPixels[py * w + px] = (uint8_t)b;
      }

      // ---- 6. Clear fb + render persistence ----
      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);
      for (int y = 0; y < h; y++)
      {
        for (int x = 0; x < w; x++)
        {
          const uint8_t v = mPixels[y * w + x];
          if (v > 0)
            fb.pixel(v, mWorldLeft + x, mWorldBottom + y);
        }
      }

      // ---- 7. Render ping rings as TILTED 3D circles ----
      // Each ping has its own axis vector (normal to ring plane).
      // The ring is the great circle of the wavefront sphere in
      // the plane perpendicular to that axis, projected through
      // the same projectSphere pipeline as the tap dots — so each
      // ring lies at a particular pitch in sphere-local 3D space
      // and rotates with the sphere's passive tumble. Visually:
      // planetary rings emanating from origin and expanding
      // outward until off-screen.
      for (int p = 0; p < mPingCount; p++)
      {
        const Ping *ping = &mPings[p];
        const float r3D = (float)ping->age * kPingSpeedDist;
        const float screenR = r3D * sphereRad;
        if (screenR < 0.5f || screenR > (float)(w + h)) continue;
        const int brightness = 7 - (7 * ping->age) / kPingMaxAge;
        if (brightness <= 0) continue;

        // Build orthonormal basis (u, v) perpendicular to the
        // ping's axis. Pick a reference vector not parallel to
        // axis so the cross product is well-conditioned.
        const float axX = ping->axisX;
        const float axY = ping->axisY;
        const float axZ = ping->axisZ;
        float refX, refY, refZ;
        if (fabsf(axY) < 0.9f) { refX = 0.0f; refY = 1.0f; refZ = 0.0f; }
        else                    { refX = 1.0f; refY = 0.0f; refZ = 0.0f; }
        // u = normalize(cross(ref, axis))
        float ux = refY * axZ - refZ * axY;
        float uy = refZ * axX - refX * axZ;
        float uz = refX * axY - refY * axX;
        const float uLen = sqrtf(ux*ux + uy*uy + uz*uz);
        if (uLen > 1e-5f) { ux /= uLen; uy /= uLen; uz /= uLen; }
        // v = cross(axis, u)
        const float vx = axY * uz - axZ * uy;
        const float vy = axZ * ux - axX * uz;
        const float vz = axX * uy - axY * ux;

        const int steps = (int)(6.2832f * screenR + 8.0f);
        const float invSteps = 1.0f / (float)steps;
        for (int s = 0; s < steps; s++)
        {
          const float a = (float)s * invSteps * 6.2832f;
          const float ca = cosf(a);
          const float sa = sinf(a);
          // Point on ring in 3D sphere-local space (radius r3D).
          const float p3x = r3D * (ux * ca + vx * sa);
          const float p3y = r3D * (uy * ca + vy * sa);
          const float p3z = r3D * (uz * ca + vz * sa);
          int px, py;
          float pz;
          projectSphere(p3x, p3y, p3z, sphereRot, sphereRad,
                        &px, &py, &pz);
          if (px >= 0 && px < w && py >= 0 && py < h)
          {
            fb.pixel(brightness, mWorldLeft + px, mWorldBottom + py);
          }
        }
      }

      // ---- 8. Listener trace as relative-to-current trail ----
      // Each past listener phase maps to an equatorial point at
      // that longitude. The sphere counter-rotates with listener
      // motion (sphereRot), so the most recent trace samples
      // settle at a near-fixed screen position while older ones
      // appear to fall back behind the listener marker.
      const int traceSize = mpNetwork->getListenerTraceSize();
      for (int i = 0; i < traceSize; i++)
      {
        const float oldPhase = mpNetwork->getListenerTracePhase(i);
        const float phiTrace = twoPi * oldPhase;
        const float sx = cosf(phiTrace);
        const float sy = 0.0f;
        const float sz = sinf(phiTrace);
        int px, py;
        float pz;
        projectSphere(sx, sy, sz, sphereRot, sphereRad,
                      &px, &py, &pz);
        if (px < 0 || px >= w || py < 0 || py >= h) continue;
        int brightness = 3 + (8 * i) / traceSize;
        if (brightness > 11) brightness = 11;
        fb.pixel(brightness, mWorldLeft + px, mWorldBottom + py);
      }

      // ---- 8b. Connectivity shell ----
      // Each feedback-selected tap (mFbWeight != 0) is a "shell
      // node." Connect each shell node to its 2 nearest neighbors
      // among other shell nodes via thin dim lines. Higher conn →
      // more selected taps → denser shell forming around listener.
      // Skip back-of-sphere nodes (pz < 0) so we only see the
      // front face of the shell.
      //
      // Shell membership and line brightness both ride on the
      // per-tap mShellLevel fade, so taps joining/leaving the
      // feedback set ramp in and out over ~200ms / ~300ms rather
      // than popping.
      {
        // Update viz-side fade levels for all taps. Asymmetric
        // rates feel more deliberate than symmetric ones; out
        // slightly slower so departures linger as ghost edges.
        const float kFadeInRate  = 1.0f / 12.0f;
        const float kFadeOutRate = 1.0f / 18.0f;
        for (int t = 0; t < kMaxNetworkTaps; t++)
        {
          const bool isFb =
            (t < activeCount) && (mpNetwork->getFbWeight(t) != 0.0f);
          // First use after constructor: snap to current state
          // instead of fading in from 0 (defends against framework
          // graphic re-instantiation).
          if (mShellLevel[t] < 0.0f)
          {
            mShellLevel[t] = isFb ? 1.0f : 0.0f;
          }
          else if (isFb)
          {
            mShellLevel[t] += kFadeInRate;
            if (mShellLevel[t] > 1.0f) mShellLevel[t] = 1.0f;
          }
          else
          {
            mShellLevel[t] -= kFadeOutRate;
            if (mShellLevel[t] < 0.0f) mShellLevel[t] = 0.0f;
          }
        }

        int shellPx[kMaxNetworkTaps];
        int shellPy[kMaxNetworkTaps];
        int shellT [kMaxNetworkTaps];
        int shellCount = 0;
        for (int t = 0; t < kMaxNetworkTaps; t++)
        {
          if (mShellLevel[t] < 0.05f) continue;
          float sx, sy, sz;
          tapToSphere(t, mTapZSmoothed[t], &sx, &sy, &sz);
          int px, py;
          float pz;
          projectSphere(sx, sy, sz, sphereRot, sphereRad,
                        &px, &py, &pz);
          if (px < 0 || px >= w || py < 0 || py >= h) continue;
          // No back-face skip — the viewer is OUTSIDE the sphere
          // looking in, so connectivity edges on the far side
          // should still be drawn (they'll just connect to whatever
          // shell nodes are nearby in screen space).
          shellPx[shellCount] = px;
          shellPy[shellCount] = py;
          shellT [shellCount] = t;
          shellCount++;
        }
        // Connect each shell node to its 2 nearest neighbors. Edge
        // brightness scales with the geometric mean of the two
        // endpoints' fade levels — both taps fully faded-in →
        // brightness 6, either fading → dimmer.
        for (int i = 0; i < shellCount; i++)
        {
          int n1 = -1, n2 = -1;
          long d1 = 1L << 30, d2 = 1L << 30;
          for (int j = 0; j < shellCount; j++)
          {
            if (i == j) continue;
            const long dx = shellPx[i] - shellPx[j];
            const long dy = shellPy[i] - shellPy[j];
            const long d = dx * dx + dy * dy;
            if (d < d1) { n2 = n1; d2 = d1; n1 = j; d1 = d; }
            else if (d < d2) { n2 = j; d2 = d; }
          }
          const float lvlI = mShellLevel[shellT[i]];
          if (n1 >= 0 && i < n1)
          {
            const float gm = sqrtf(lvlI * mShellLevel[shellT[n1]]);
            int b = 1 + (int)(gm * 5.5f + 0.5f);
            if (b > 6) b = 6;
            if (b >= 1)
              drawLine(fb, shellPx[i], shellPy[i],
                       shellPx[n1], shellPy[n1], b);
          }
          if (n2 >= 0 && i < n2)
          {
            const float gm = sqrtf(lvlI * mShellLevel[shellT[n2]]);
            int b = 1 + (int)(gm * 5.5f + 0.5f);
            if (b > 6) b = 6;
            if (b >= 1)
              drawLine(fb, shellPx[i], shellPy[i],
                       shellPx[n2], shellPy[n2], b);
          }
        }
      }

      // ---- 9. Render tap dots ----
      const int flashMax = mpNetwork->getRicochetFlashMax();
      for (int t = 0; t < activeCount; t++)
      {
        float sx, sy, sz;
        tapToSphere(t, mTapZSmoothed[t], &sx, &sy, &sz);
        // REVERSE taps counter-rotate (see trail-deposit comment).
        const int modeForRot = mpNetwork->getTapMode(t);
        if (modeForRot == NETWORK_TAP_REVERSE)
        {
          const float ar = -2.0f * (mRotAngle + sphereRot);
          const float cr = cosf(ar);
          const float sr = sinf(ar);
          const float nsx = sx * cr + sz * sr;
          const float nsz = -sx * sr + sz * cr;
          sx = nsx; sz = nsz;
        }
        int px, py;
        float pz;
        projectSphere(sx, sy, sz, sphereRot, sphereRad,
                      &px, &py, &pz);
        if (px < 0 || px >= w || py < 0 || py >= h) continue;

        const int mode = mpNetwork->getTapMode(t);
        int targetBrightness;
        bool hollow = false;
        switch (mode)
        {
          case NETWORK_TAP_MUTE:    targetBrightness = 3; break;
          case NETWORK_TAP_CRUSH:
          {
            uint32_t hh = (uint32_t)t * 2654435761u +
                          (uint32_t)mFrameCounter;
            hh = hh * 1103515245u + 12345u;
            targetBrightness = ((hh >> 16) & 1u) ? 13 : 7;
            break;
          }
          case NETWORK_TAP_REVERSE: targetBrightness = 11; hollow = true; break;
          case NETWORK_TAP_NORMAL:  targetBrightness = 9 + wetBoost; break;
          case NETWORK_TAP_STUTTER:
          {
            // Flash bright (15) for 2 frames after each loop
            // iteration start, then drop to dim base (8). Combined
            // with the per-iteration ghost deposited into the
            // persistence buffer, the tap reads as a flickering
            // head trailed by a fading dotted line of past
            // iteration positions.
            const int ff = (int)mStutterFlicker[t];
            targetBrightness = (ff < 2) ? 15 : 8;
            break;
          }
          case NETWORK_TAP_SCRUB:
          default:                  targetBrightness = 12; break;
        }

        // Smooth mode-derived brightness across walker-wrap mode
        // reshuffles. STUTTER bypasses smoothing because its
        // per-iteration flash must be instant; the smoothed value
        // is snapped to the current STUTTER target so any
        // subsequent transition OUT of STUTTER starts at the right
        // value instead of a stale one.
        // First use after constructor: snap to target (defends
        // against framework graphic re-instantiation, which would
        // otherwise visibly dim all tap dots toward the 9.0 init
        // value and ramp back up to 9 + wetBoost).
        int brightness;
        if (mode == NETWORK_TAP_STUTTER || mTapBrightSmoothed[t] < 0.0f)
        {
          mTapBrightSmoothed[t] = (float)targetBrightness;
          brightness = targetBrightness;
        }
        else
        {
          mTapBrightSmoothed[t] +=
            ((float)targetBrightness - mTapBrightSmoothed[t]) * 0.25f;
          brightness = (int)(mTapBrightSmoothed[t] + 0.5f);
        }

        // Depth cue for back-of-sphere taps — gentle. The viewer
        // is OUTSIDE the sphere looking in, so taps on the far side
        // should always be visible (just slightly dimmer than the
        // front face). Max dim of 1 brightness step at pz = −1,
        // and brightness floors at 3 so MUTE taps stay readable on
        // the back hemisphere instead of dropping to near-invisible.
        if (pz < 0.0f)
        {
          const int dim = (int)((-pz) * 1.0f + 0.5f);
          brightness -= dim;
          if (brightness < 3) brightness = 3;
        }

        const int rflash = mpNetwork->getRicochetFlash(t);
        if (rflash > 0 && flashMax > 0)
          brightness += (rflash * 5) / flashMax;
        const int pflash = (int)mTapPingFlash[t];
        if (pflash > 0)
          brightness += (pflash * 6) / kPingFlashFrames;
        if (brightness > 15) brightness = 15;
        if (brightness < 0)  brightness = 0;

        if (hollow)
        {
          for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
              if (dx == 0 && dy == 0) continue;
              const int rpx = px + dx;
              const int rpy = py + dy;
              if (rpx >= 0 && rpx < w && rpy >= 0 && rpy < h)
                fb.pixel(brightness,
                         mWorldLeft + rpx, mWorldBottom + rpy);
            }
        }
        else
        {
          fb.pixel(brightness, mWorldLeft + px, mWorldBottom + py);
        }
      }

      // ---- 10. Listener marker at view center ----
      const int viewCx = w / 2;
      const int viewCy = h / 2;
      if (viewCx >= 1 && viewCx < w - 1 && viewCy >= 1 && viewCy < h - 1)
      {
        fb.pixel(WHITE, mWorldLeft + viewCx,     mWorldBottom + viewCy);
        fb.pixel(11,    mWorldLeft + viewCx + 1, mWorldBottom + viewCy);
        fb.pixel(11,    mWorldLeft + viewCx - 1, mWorldBottom + viewCy);
        fb.pixel(11,    mWorldLeft + viewCx,     mWorldBottom + viewCy + 1);
        fb.pixel(11,    mWorldLeft + viewCx,     mWorldBottom + viewCy - 1);
      }
    }
#endif
  };
} // namespace stolmine
