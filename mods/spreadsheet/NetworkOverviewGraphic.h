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
      for (int i = 0; i < kMaxPings; i++) mPings[i].age = 0;
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
    struct Ping { int age; };
    Ping mPings[kMaxPings];
    int  mPingCount;

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

    // Map tap to a 3D point on the unit sphere (see file header).
    // Latitude: golden-ratio LDS on tap index (uniform spread at any
    // density). Longitude: actual reflector azimuth (illustrative).
    // zGlitch lifts the point radially off the sphere surface.
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

      // Sphere rotation: constant-rate accumulator scaled by
      // motion. At motion=1 sphere rotates 0.25 revolutions/sec
      // (4s per full rotation, matches walker base rate). At
      // motion=0 sphere holds still. No jerk regardless of the
      // walker's chaotic instantaneous velocity.
      const float motionNorm = mpNetwork->getMotionNorm();
      {
        const float kSphereRotPerFrameAtFullMotion =
          -0.25f * 6.2832f / 60.0f;
        mSphereRotAccumulator +=
          motionNorm * kSphereRotPerFrameAtFullMotion;
        // Keep within a sane numerical range to avoid drift.
        while (mSphereRotAccumulator < -6.2832f)
          mSphereRotAccumulator += 6.2832f;
        while (mSphereRotAccumulator >  6.2832f)
          mSphereRotAccumulator -= 6.2832f;
      }
      const float sphereRot = mSphereRotAccumulator;

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
            mPings[mPingCount].age = 0;
            mPingCount++;
          }
        }
      }

      // Ping radial expansion speed scaled by motion. Floor 0.1
      // so rings still slowly expand at near-zero motion (rather
      // than freezing in place); at motion=1 the previous full
      // speed is restored.
      const float motionScale = 0.1f + motionNorm * 0.9f;
      const float kPingSpeedDist =
        (2.6f / (float)kPingMaxAge) * motionScale;
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

      // ---- 7. Render ping rings (centered on view) ----
      // BISECTION step 1: cap visible ring radius to 0.5 × sphereRad
      // so the ring never extends beyond the sphere outline. If this
      // diminishes the "viewer zooming in / pop back" effect, the
      // cause is ring extent past the sphere. If unchanged, bisect
      // brightness next.
      const int viewCx = w / 2;
      const int viewCy = h / 2;
      const float kPingMaxVisibleScreenR = sphereRad * 0.5f;
      for (int p = 0; p < mPingCount; p++)
      {
        const Ping *ping = &mPings[p];
        const float r3D = (float)ping->age * kPingSpeedDist;
        const float screenR = r3D * sphereRad;
        if (screenR < 0.5f || screenR > kPingMaxVisibleScreenR) continue;
        const int brightness = 7 - (7 * ping->age) / kPingMaxAge;
        if (brightness <= 0) continue;
        const int steps = (int)(6.2832f * screenR + 8.0f);
        const float invSteps = 1.0f / (float)steps;
        for (int s = 0; s < steps; s++)
        {
          const float a = (float)s * invSteps * 6.2832f;
          const int px = viewCx + (int)(screenR * cosf(a));
          const int py = viewCy + (int)(screenR * sinf(a));
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
