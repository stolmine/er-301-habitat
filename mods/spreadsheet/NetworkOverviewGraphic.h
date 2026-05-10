#pragma once

// Network overview viz — sonar + per-tap activity hybrid.
//
// Listener emits sonar pings (concentric rings expanding from
// current listener position) at a steady cadence. When a ring's
// 3D radius equals the distance to a reflector, that reflector
// flashes — the ping has "arrived." Glitch modes modulate the
// flash signature (silent for MUTE, sustained for STUTTER, etc).
//
// Layered render (back-to-front):
//   1. Persistence buffer fade (stutter ghosts only).
//   2. Stutter wrap → ghost spawn into persistence.
//   3. Sonar ping update: spawn new ping on cadence; for each ping,
//      detect reflector crossings → set mTapPingFlash; render ring.
//   4. Persistence rendered.
//   5. Listener trajectory trace (fading by age).
//   6. Tap dots — mode color, ping flash boost, ricochet flash boost,
//      wet-level pulse, mode-encoded Z displacement.
//   7. Listener marker (cross at current orbit position).
//
// Size knob contracts/expands the entire disc (geomScale).
// Phase-space layer removed in this redesign — sonar provides the
// rhythmic visual energy phase-space couldn't deliver for a
// reverb-like signal.
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
        mPings[i].emitX = 0.0f;
        mPings[i].emitY = 0.0f;
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

    // ---- Sonar state ----
    // Rings emitted from listener position, expanding at constant
    // 3D-units/frame. Each ping has its own emit position so it
    // continues from where it was spawned (listener may have moved
    // by the next ping).
    static const int   kMaxPings        = 4;
    static const int   kPingMaxAge      = 90;     // frames; ~1.5s @60fps
    static const int   kPingFlashFrames = 12;     // per-tap flash dur
    struct Ping { float emitX, emitY; int age; };
    Ping mPings[kMaxPings];
    int  mPingCount;
    int  mPingTimer;

    // Per-tap flash from ping crossings.
    uint8_t mTapPingFlash[kMaxNetworkTaps];

    // Project a 3D point through current rotation/tilt to a 2D pixel.
    inline void project3D(float x, float y, float z, float scale,
                          int *outPx, int *outPy, float *outDepth) const
    {
      const float cosA = cosf(mRotAngle);
      const float sinA = sinf(mRotAngle);
      const float costilt = 0.9553f;
      const float sintilt = 0.2955f;
      const float rx    = x * cosA + z * sinA;
      const float rzNew = -x * sinA + z * cosA;
      const float ry    = y;
      const float fx = rx;
      const float fy = ry * costilt - rzNew * sintilt;
      const float depth = rzNew * costilt + ry * sintilt;
      const int w = mWidth < kMaxW ? mWidth : kMaxW;
      const int h = mHeight < kMaxH ? mHeight : kMaxH;
      *outPx = (int)(fx * scale) + w / 2;
      *outPy = (int)(fy * scale) + h / 2;
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

      // Disc render scale follows Size knob.
      const float sizeNorm = mpNetwork->getSizeNorm();
      const float kBaseScale = 26.0f;
      const float geomScale = sizeNorm * kBaseScale;

      // Listener current position (3D).
      const float listenerPhase = mpNetwork->getListenerPhase();
      const float kListenerR = 1.3f;
      const float listenerX = cosf(2.0f * 3.14159265f * listenerPhase) * kListenerR;
      const float listenerY = sinf(2.0f * 3.14159265f * listenerPhase) * kListenerR;

      // Wet-level boost — RMS of last 16 ring samples gives a
      // global "intensity" used to pulse NORMAL tap brightness.
      float wetLvlSq = 0.0f;
      const int ringSize = mpNetwork->getOutputRingSize();
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

      // ---- 2. Stutter wrap → ghost spawn into persistence ----
      const int activeCount = mpNetwork->getActiveTapCount();
      for (int t = 0; t < activeCount; t++)
      {
        const int curIter = mpNetwork->getTapStutterIter(t);
        const int lastIter = (int)mLastStutterIter[t];
        if (curIter > 0 && curIter < lastIter)
        {
          const float rx = mpNetwork->getReflectorX(t);
          const float ry = mpNetwork->getReflectorY(t);
          uint32_t hg = (uint32_t)t * 2654435761u +
                        (uint32_t)curIter;
          hg = hg * 1103515245u + 12345u;
          const float angle =
            (float)((hg >> 16) & 0xFFFFu) * (6.2832f / 65535.0f);
          const float ghostRadius = 0.18f;
          const float gx = rx + ghostRadius * cosf(angle);
          const float gy = ry + ghostRadius * sinf(angle);
          int px, py;
          float pz;
          project3D(gx, gy, 0.0f, geomScale, &px, &py, &pz);
          if (px >= 0 && px < w && py >= 0 && py < h)
          {
            mPixels[py * w + px] = 12;
          }
        }
        mLastStutterIter[t] = (uint8_t)curIter;
      }

      // ---- 3. Sonar pings ----
      // Decay per-tap ping-flash counters.
      for (int t = 0; t < kMaxNetworkTaps; t++)
      {
        if (mTapPingFlash[t] > 0) mTapPingFlash[t]--;
      }

      // Spawn new ping on cadence.
      mPingTimer--;
      if (mPingTimer <= 0)
      {
        // ~24-frame period → ~2.5 pings/sec at 60fps.
        mPingTimer = 24;
        if (mPingCount < kMaxPings)
        {
          mPings[mPingCount].emitX = listenerX;
          mPings[mPingCount].emitY = listenerY;
          mPings[mPingCount].age = 0;
          mPingCount++;
        }
      }

      // Update ping state: detect reflector crossings, age each
      // ping, drop expired. (Render below, after fb clear.)
      // Ping speed: covers full unit-disc diagonal over kPingMaxAge.
      const float kPingSpeed3D = 2.6f / (float)kPingMaxAge;
      for (int p = 0; p < mPingCount; )
      {
        Ping *ping = &mPings[p];
        const float r3D     = (float)ping->age       * kPingSpeed3D;
        const float r3DPrev = (float)(ping->age - 1) * kPingSpeed3D;

        for (int t = 0; t < activeCount; t++)
        {
          if (mpNetwork->getTapMode(t) == NETWORK_TAP_MUTE)
            continue;
          const float dx = mpNetwork->getReflectorX(t) - ping->emitX;
          const float dy = mpNetwork->getReflectorY(t) - ping->emitY;
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

      // Clear framebuffer.
      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);

      // ---- 5. Render persistence (stutter ghosts) ----
      for (int y = 0; y < h; y++)
      {
        for (int x = 0; x < w; x++)
        {
          const uint8_t v = mPixels[y * w + x];
          if (v > 0)
            fb.pixel(v, mWorldLeft + x, mWorldBottom + y);
        }
      }

      // ---- 6. Re-render ping rings (state already updated above) ----
      for (int p = 0; p < mPingCount; p++)
      {
        const Ping *ping = &mPings[p];
        const float r3D = (float)ping->age * kPingSpeed3D;
        int cx, cy;
        float cz;
        project3D(ping->emitX, ping->emitY, 0.0f,
                  geomScale, &cx, &cy, &cz);
        const float screenR = r3D * geomScale;
        if (screenR < 0.5f) continue;
        const int brightness = 7 - (7 * ping->age) / kPingMaxAge;
        if (brightness <= 0) continue;
        const int steps = (int)(6.2832f * screenR + 8.0f);
        const float invSteps = 1.0f / (float)steps;
        for (int s = 0; s < steps; s++)
        {
          const float a = (float)s * invSteps * 6.2832f;
          const int px = cx + (int)(screenR * cosf(a));
          const int py = cy + (int)(screenR * sinf(a));
          if (px >= 0 && px < w && py >= 0 && py < h)
          {
            fb.pixel(brightness, mWorldLeft + px, mWorldBottom + py);
          }
        }
      }

      // ---- 7. Listener trajectory trace ----
      const int traceSize = mpNetwork->getListenerTraceSize();
      for (int i = 0; i < traceSize; i++)
      {
        const float phase = mpNetwork->getListenerTracePhase(i);
        const float tx = cosf(2.0f * 3.14159265f * phase) * kListenerR;
        const float ty = sinf(2.0f * 3.14159265f * phase) * kListenerR;
        int px, py;
        float pz;
        project3D(tx, ty, 0.0f, geomScale, &px, &py, &pz);
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
        int px, py;
        float pz;
        project3D(rx, ry, rz, geomScale, &px, &py, &pz);
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

        // Ricochet flash boost.
        const int rflash = mpNetwork->getRicochetFlash(t);
        if (rflash > 0 && flashMax > 0)
        {
          brightness += (rflash * 5) / flashMax;
        }
        // Ping flash boost (when sonar ring crosses reflector).
        const int pflash = (int)mTapPingFlash[t];
        if (pflash > 0)
        {
          brightness += (pflash * 6) / kPingFlashFrames;
        }
        if (brightness > 15) brightness = 15;

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

      // ---- 9. Listener marker ----
      int lpx, lpy;
      float lpz;
      project3D(listenerX, listenerY, 0.0f,
                geomScale, &lpx, &lpy, &lpz);
      if (lpx >= 1 && lpx < w - 1 && lpy >= 1 && lpy < h - 1)
      {
        fb.pixel(WHITE, mWorldLeft + lpx,     mWorldBottom + lpy);
        fb.pixel(11,    mWorldLeft + lpx + 1, mWorldBottom + lpy);
        fb.pixel(11,    mWorldLeft + lpx - 1, mWorldBottom + lpy);
        fb.pixel(11,    mWorldLeft + lpx,     mWorldBottom + lpy + 1);
        fb.pixel(11,    mWorldLeft + lpx,     mWorldBottom + lpy - 1);
      }
    }
#endif
  };
} // namespace stolmine
