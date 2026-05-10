#pragma once

// Network overview viz — listener-centered sphere + sonar.
//
// Reframes the spatial model: listener is fixed at view center,
// reflectors are mapped to an invisible unit sphere around them.
// As the listener orbits the audio field, the sphere appears to
// rotate around the central viewpoint. Glitch Z displacement
// lifts reflectors off the sphere surface as a screen-y offset.
//
// Sonar pings expand from center (listener) as concentric screen-
// space rings. Each reflector retains its actual world-space
// distance from listener; reflectors flash when ping_actual_radius
// equals their actual_distance, so pings sweep through reflectors
// in distance order even though their visual positions are
// equidistant on the unit sphere.
//
// Layered render (back-to-front):
//   1. Persistence buffer fade (stutter ghosts only).
//   2. Stutter wrap → ghost spawn into persistence at sphere pos.
//   3. Sonar ping update: spawn, detect reflector crossings, age.
//   4. Rotation advance.
//   5. Clear fb + render persistence.
//   6. Render ping rings (screen-space circles centered on view).
//   7. Render listener trajectory trace as sphere-pole movement.
//   8. Render tap dots: sphere position, mode color, ping flash,
//      ricochet flash, wet boost, glitch Z lift.
//   9. Render listener marker at view center.
//
// Size knob contracts/expands the rendered sphere radius.
//
// Header-only inline per feedback_no_out_of_line_virtuals.

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
      for (int i = 0; i < kMaxPings; i++)
      {
        mPings[i].age = 0;
      }
    }

    virtual ~NetworkOverviewGraphic()
    {
      if (mpNetwork)
        mpNetwork->release();
    }

    void follow(Network *p)
    {
      if (mpNetwork)
        mpNetwork->release();
      mpNetwork = p;
      if (mpNetwork)
        mpNetwork->attach();
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

    // Sonar — pings emanate from view center (listener) as expanding
    // screen-space rings. Each ping has an age; reflector flashes
    // are triggered by the ping's actual-distance threshold sweeping
    // past each reflector's actual distance from listener.
    static const int   kMaxPings        = 4;
    static const int   kPingMaxAge      = 90;
    static const int   kPingFlashFrames = 12;
    struct Ping { int age; };
    Ping mPings[kMaxPings];
    int  mPingCount;
    int  mPingTimer;

    uint8_t mTapPingFlash[kMaxNetworkTaps];

    // Compute listener-frame coords (right, up, forward) for a
    // world reflector position with optional Z displacement.
    // Returns actual distance via *outActualDist.
    inline void worldToListener(float rx, float ry, float rz,
                                float cosM, float sinM,
                                float listenerX, float listenerY,
                                float *outR, float *outU, float *outF,
                                float *outActualDist) const
    {
      const float vx = rx - listenerX;
      const float vy = ry - listenerY;
      const float vz = rz;
      // Listener forward = -(cosM, sinM, 0); right = (-sinM, cosM, 0).
      *outR = -vx * sinM + vy * cosM;
      *outU = vz;
      *outF = -vx * cosM - vy * sinM;
      const float d2 = (*outR)*(*outR) + (*outU)*(*outU) + (*outF)*(*outF);
      *outActualDist = sqrtf(d2);
    }

    // Project listener-frame 3D point through sphere normalization
    // + view tumble + screen scale to a 2D pixel.
    // sphereRad: render scale in pixels (spheres unit radius mapped here)
    // Returns view-z depth via *outDepth (front-of-sphere = positive).
    inline void projectSphere(float lr, float lu, float lf,
                              float dist, float sphereRad,
                              int *outPx, int *outPy, float *outDepth) const
    {
      // Normalize to unit sphere.
      float sx, sy, sz;
      if (dist > 0.001f)
      {
        const float invD = 1.0f / dist;
        sx = lr * invD;
        sy = lu * invD;
        sz = lf * invD;
      }
      else
      {
        sx = sy = sz = 0.0f;
      }
      // View tumble around Y axis.
      const float cosA = cosf(mRotAngle);
      const float sinA = sinf(mRotAngle);
      const float rx    =  sx * cosA + sz * sinA;
      const float rzNew = -sx * sinA + sz * cosA;
      const float ry    =  sy;
      // X-axis tilt.
      const float costilt = 0.9553f;
      const float sintilt = 0.2955f;
      const float fx = rx;
      const float fy = ry * costilt - rzNew * sintilt;
      const float depth = rzNew * costilt + ry * sintilt;
      const int w = mWidth < kMaxW ? mWidth : kMaxW;
      const int h = mHeight < kMaxH ? mHeight : kMaxH;
      *outPx = (int)(fx * sphereRad) + w / 2;
      *outPy = (int)(fy * sphereRad) + h / 2;
      *outDepth = depth;
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
          const float p = mpNetwork->getTapStutterPosNorm(t);
          return 0.5f * sinf(2.0f * 3.14159265f * p);
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

      // Sphere render scale follows Size knob.
      const float sizeNorm = mpNetwork->getSizeNorm();
      const float kBaseScale = 26.0f;
      const float sphereRad = sizeNorm * kBaseScale;

      // Listener world position (orbit at radius 1.3).
      const float listenerPhase = mpNetwork->getListenerPhase();
      const float kListenerR = 1.3f;
      const float twoPi = 2.0f * 3.14159265f;
      const float cosM = cosf(twoPi * listenerPhase);
      const float sinM = sinf(twoPi * listenerPhase);
      const float listenerX = cosM * kListenerR;
      const float listenerY = sinM * kListenerR;

      // Wet-level pulse from output ring RMS.
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
          const float rx = mpNetwork->getReflectorX(t);
          const float ry = mpNetwork->getReflectorY(t);
          const float rz = tapZ(t);
          float lr, lu, lf, dist;
          worldToListener(rx, ry, rz, cosM, sinM,
                          listenerX, listenerY,
                          &lr, &lu, &lf, &dist);
          // Ghost offset: small angular jitter on sphere surface.
          uint32_t hg = (uint32_t)t * 2654435761u +
                        (uint32_t)curIter;
          hg = hg * 1103515245u + 12345u;
          const float jitter =
            ((float)((hg >> 16) & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f) * 0.2f;
          int px, py;
          float pz;
          projectSphere(lr + jitter, lu, lf, dist, sphereRad,
                        &px, &py, &pz);
          if (px >= 0 && px < w && py >= 0 && py < h)
          {
            mPixels[py * w + px] = 12;
          }
        }
        mLastStutterIter[t] = (uint8_t)curIter;
      }

      // ---- 3. Sonar pings ----
      // Decay flash counters.
      for (int t = 0; t < kMaxNetworkTaps; t++)
      {
        if (mTapPingFlash[t] > 0) mTapPingFlash[t]--;
      }

      // Spawn cadence.
      mPingTimer--;
      if (mPingTimer <= 0)
      {
        mPingTimer = 24;
        if (mPingCount < kMaxPings)
        {
          mPings[mPingCount].age = 0;
          mPingCount++;
        }
      }

      // Ping speed in actual-distance space: covers 2.6 (max
      // listener-to-reflector distance) over kPingMaxAge frames.
      const float kPingSpeedDist = 2.6f / (float)kPingMaxAge;
      // Update + age + reflector-cross detection.
      for (int p = 0; p < mPingCount; )
      {
        Ping *ping = &mPings[p];
        const float r3D     = (float)ping->age       * kPingSpeedDist;
        const float r3DPrev = (float)(ping->age - 1) * kPingSpeedDist;

        for (int t = 0; t < activeCount; t++)
        {
          if (mpNetwork->getTapMode(t) == NETWORK_TAP_MUTE) continue;
          const float rx = mpNetwork->getReflectorX(t);
          const float ry = mpNetwork->getReflectorY(t);
          const float rz = tapZ(t);
          float lr, lu, lf, dist;
          worldToListener(rx, ry, rz, cosM, sinM,
                          listenerX, listenerY,
                          &lr, &lu, &lf, &dist);
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

      // ---- 5. Clear fb + render persistence ----
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

      // ---- 6. Render ping rings (centered on view = listener) ----
      // Ping is rendered at screen-space radius proportional to its
      // actual-distance radius. Scale: same as sphere render scale
      // so the ring at distance=1.0 sits at the sphere's apparent
      // radius. Ping reaches max ring at distance=2.6 ≈ 2.6×sphereRad.
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

      // ---- 7. Listener trace as orbit-relative trail ----
      // Past walker positions are rendered RELATIVE TO CURRENT
      // listener (i.e., in listener frame). They show "where the
      // field has been" recently, around the central listener.
      const int traceSize = mpNetwork->getListenerTraceSize();
      for (int i = 0; i < traceSize; i++)
      {
        // Sample old listener world pos.
        const float oldPhase = mpNetwork->getListenerTracePhase(i);
        const float oldX = cosf(twoPi * oldPhase) * kListenerR;
        const float oldY = sinf(twoPi * oldPhase) * kListenerR;
        // Treat the OLD listener as a "field point" relative to
        // CURRENT listener. Transform through current listener frame.
        float lr, lu, lf, dist;
        worldToListener(oldX, oldY, 0.0f, cosM, sinM,
                        listenerX, listenerY,
                        &lr, &lu, &lf, &dist);
        if (dist < 0.001f) continue;
        int px, py;
        float pz;
        projectSphere(lr, lu, lf, dist, sphereRad, &px, &py, &pz);
        if (px < 0 || px >= w || py < 0 || py >= h) continue;
        int brightness = 3 + (8 * i) / traceSize;
        if (brightness > 11) brightness = 11;
        fb.pixel(brightness, mWorldLeft + px, mWorldBottom + py);
      }

      // ---- 8. Render tap dots ----
      const int flashMax = mpNetwork->getRicochetFlashMax();
      for (int t = 0; t < activeCount; t++)
      {
        const float rx = mpNetwork->getReflectorX(t);
        const float ry = mpNetwork->getReflectorY(t);
        const float rz = tapZ(t);
        float lr, lu, lf, dist;
        worldToListener(rx, ry, rz, cosM, sinM,
                        listenerX, listenerY,
                        &lr, &lu, &lf, &dist);
        if (dist < 0.001f) continue;
        int px, py;
        float pz;
        projectSphere(lr, lu, lf, dist, sphereRad, &px, &py, &pz);
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

        // Depth dim: back-of-sphere darker than front.
        // pz in roughly [-1, 1]; front (pz>0) keeps brightness,
        // back fades by up to 4.
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

      // ---- 9. Listener marker (always at view center) ----
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
