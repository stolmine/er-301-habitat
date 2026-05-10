#pragma once

// Network overview viz — listener-centered sphere with per-tap
// trails and sonar pings.
//
// Reflectors are mapped to a unit sphere by treating their 2D
// disk position as longitude/latitude:
//   longitude  = atan2(ry, rx)            // covers full [-π, π]
//   latitude   = (disk_radius - 0.5) × π  // covers [-π/2, π/2]
// This guarantees a phyllotaxis-style spread across the whole
// sphere instead of the front-hemisphere clustering you'd get from
// a naive listener-relative projection. Glitch Z displacement
// scales the sphere radius locally per tap (lifts/pushes off
// surface).
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
      mRotAngle = 0.0f;
      mFrameCounter = 0;
      mPingCount = 0;
      mPingTimer = 0;
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
    int  mPingTimer;

    uint8_t mTapPingFlash[kMaxNetworkTaps];

    // Map reflector world-disk position to a 3D point on the
    // unit sphere. Glitch Z lifts radially.
    inline void diskToSphere(float rx, float ry, float zGlitch,
                             float *sx, float *sy, float *sz) const
    {
      const float phi = atan2f(ry, rx);
      const float r = sqrtf(rx * rx + ry * ry);
      const float theta = (r - 0.5f) * 3.14159265f;
      const float ct = cosf(theta);
      const float st = sinf(theta);
      const float radial = 1.0f + zGlitch * 0.3f;
      *sx = ct * cosf(phi) * radial;
      *sy = st             * radial;
      *sz = ct * sinf(phi) * radial;
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

    // Per-tap effective disc position with glitch-mode displacement.
    // STUTTER taps orbit a small circle around their reflector pos
    // in disc plane (instead of radial Z pulse) — the circular
    // motion reads as continuous orbital movement on the sphere
    // rather than the previous zig-zag.
    inline void getEffectiveDiscPos(int t,
                                    float *outX, float *outY, float *outZ) const
    {
      const float rx = mpNetwork->getReflectorX(t);
      const float ry = mpNetwork->getReflectorY(t);
      const int mode = mpNetwork->getTapMode(t);
      *outX = rx;
      *outY = ry;
      *outZ = tapZ(t);
      if (mode == NETWORK_TAP_STUTTER)
      {
        const float p = mpNetwork->getTapStutterPosNorm(t);
        const float orbitR = 0.12f;
        const float phase = p * 6.2832f;
        *outX += cosf(phase) * orbitR;
        *outY += sinf(phase) * orbitR;
        *outZ = 0.0f;   // replace Z with disc-plane orbit
      }
    }

    inline float tapZ(int t) const
    {
      const int mode = mpNetwork->getTapMode(t);
      switch (mode)
      {
        case NETWORK_TAP_NORMAL: return 0.0f;
        case NETWORK_TAP_MUTE:   return -1.0f;
        case NETWORK_TAP_STUTTER:
        {
          // Z is overridden by orbit in getEffectiveDiscPos.
          return 0.0f;
        }
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
        case NETWORK_TAP_REVERSE: return -0.4f;
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
      const float sphereRad = sizeNorm * kBaseScale;

      // Listener world position + listener-motion sphere rotation.
      const float listenerPhase = mpNetwork->getListenerPhase();
      const float kListenerR = 1.3f;
      const float twoPi = 2.0f * 3.14159265f;
      const float cosM = cosf(twoPi * listenerPhase);
      const float sinM = sinf(twoPi * listenerPhase);
      const float listenerX = cosM * kListenerR;
      const float listenerY = sinM * kListenerR;
      // As listener orbits, sphere appears to rotate opposite
      // direction (visual response to listener motion).
      const float sphereRot = -twoPi * listenerPhase;

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
      for (int i = 0; i < w * h; i++)
      {
        if (mPixels[i] > 0) mPixels[i]--;
      }

      // ---- 2. Stutter wrap → ghost spawn at sphere position ----
      const int activeCount = mpNetwork->getActiveTapCount();
      for (int t = 0; t < activeCount; t++)
      {
        const int curIter = mpNetwork->getTapStutterIter(t);
        const int lastIter = (int)mLastStutterIter[t];
        if (curIter > 0 && curIter < lastIter)
        {
          float ex, ey, ez;
          getEffectiveDiscPos(t, &ex, &ey, &ez);
          float sx, sy, sz;
          diskToSphere(ex, ey, ez, &sx, &sy, &sz);
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
        mLastStutterIter[t] = (uint8_t)curIter;
      }

      // ---- 3. Sonar pings ----
      for (int t = 0; t < kMaxNetworkTaps; t++)
      {
        if (mTapPingFlash[t] > 0) mTapPingFlash[t]--;
      }

      mPingTimer--;
      if (mPingTimer <= 0)
      {
        // Slower cadence — ~48 frames between pings (1.25/sec).
        mPingTimer = 48;
        if (mPingCount < kMaxPings)
        {
          mPings[mPingCount].age = 0;
          mPingCount++;
        }
      }

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
      // Each tap deposits a low-bright pixel each frame at its
      // current sphere position, building a fading arc as the
      // sphere rotates.
      for (int t = 0; t < activeCount; t++)
      {
        float ex, ey, ez;
        getEffectiveDiscPos(t, &ex, &ey, &ez);
        float sx, sy, sz;
        diskToSphere(ex, ey, ez, &sx, &sy, &sz);
        int px, py;
        float pz;
        projectSphere(sx, sy, sz, sphereRot, sphereRad,
                      &px, &py, &pz);
        if (px < 0 || px >= w || py < 0 || py >= h) continue;
        // Skip MUTE — silent reflectors leave no trail.
        if (mpNetwork->getTapMode(t) == NETWORK_TAP_MUTE) continue;
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
      const int viewCx = w / 2;
      const int viewCy = h / 2;
      for (int p = 0; p < mPingCount; p++)
      {
        const Ping *ping = &mPings[p];
        const float r3D = (float)ping->age * kPingSpeedDist;
        const float screenR = r3D * sphereRad;
        if (screenR < 0.5f || screenR > (float)(w + h)) continue;
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
      const int traceSize = mpNetwork->getListenerTraceSize();
      for (int i = 0; i < traceSize; i++)
      {
        const float oldPhase = mpNetwork->getListenerTracePhase(i);
        // Past listener positions in world frame.
        const float oldX = cosf(twoPi * oldPhase) * kListenerR;
        const float oldY = sinf(twoPi * oldPhase) * kListenerR;
        // Map old position to sphere coords (treat as a "ghost
        // listener" in the field).
        float sx, sy, sz;
        diskToSphere(oldX, oldY, 0.0f, &sx, &sy, &sz);
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
      {
        int shellPx[kMaxNetworkTaps];
        int shellPy[kMaxNetworkTaps];
        int shellCount = 0;
        for (int t = 0; t < activeCount; t++)
        {
          if (mpNetwork->getFbWeight(t) == 0.0f) continue;
          float ex, ey, ez;
          getEffectiveDiscPos(t, &ex, &ey, &ez);
          float sx, sy, sz;
          diskToSphere(ex, ey, ez, &sx, &sy, &sz);
          int px, py;
          float pz;
          projectSphere(sx, sy, sz, sphereRot, sphereRad,
                        &px, &py, &pz);
          if (px < 0 || px >= w || py < 0 || py >= h) continue;
          if (pz < 0.0f) continue;   // hide back-of-sphere edges
          shellPx[shellCount] = px;
          shellPy[shellCount] = py;
          shellCount++;
        }
        // Connect each shell node to its 2 nearest neighbors. Lines
        // are drawn dim (4) so they read as a structural shell
        // without dominating tap dots (12-15).
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
          if (n1 >= 0 && i < n1)
            drawLine(fb, shellPx[i], shellPy[i],
                     shellPx[n1], shellPy[n1], 4);
          if (n2 >= 0 && i < n2)
            drawLine(fb, shellPx[i], shellPy[i],
                     shellPx[n2], shellPy[n2], 4);
        }
      }

      // ---- 9. Render tap dots ----
      const int flashMax = mpNetwork->getRicochetFlashMax();
      for (int t = 0; t < activeCount; t++)
      {
        float ex, ey, ez;
        getEffectiveDiscPos(t, &ex, &ey, &ez);
        float sx, sy, sz;
        diskToSphere(ex, ey, ez, &sx, &sy, &sz);
        int px, py;
        float pz;
        projectSphere(sx, sy, sz, sphereRot, sphereRad,
                      &px, &py, &pz);
        if (px < 0 || px >= w || py < 0 || py >= h) continue;

        const int mode = mpNetwork->getTapMode(t);
        int brightness;
        bool hollow = false;
        switch (mode)
        {
          case NETWORK_TAP_MUTE:    brightness = 3; break;
          case NETWORK_TAP_CRUSH:
          {
            uint32_t hh = (uint32_t)t * 2654435761u +
                          (uint32_t)mFrameCounter;
            hh = hh * 1103515245u + 12345u;
            brightness = ((hh >> 16) & 1u) ? 13 : 7;
            break;
          }
          case NETWORK_TAP_REVERSE: brightness = 11; hollow = true; break;
          case NETWORK_TAP_NORMAL:  brightness = 9 + wetBoost; break;
          case NETWORK_TAP_STUTTER:
          case NETWORK_TAP_SCRUB:
          default:                  brightness = 12; break;
        }

        // Depth dim for back-of-sphere taps.
        if (pz < 0.0f)
        {
          const int dim = (int)((-pz) * 4.0f + 0.5f);
          brightness -= dim;
          if (brightness < 1) brightness = 1;
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
