#pragma once

// Network overview viz — 3D phase-space disc.
//
// Reflects the Network unit's geometry + glitch state on the gltch
// ply. The 2D unit-disk reflector field is rendered as a slowly
// rotating disc; per-tap Z displacement encodes glitch character so
// the rotation reveals 3D structure that "comes alive" with the
// glitch macro.
//
// Layered render (back-to-front):
//   1. Persistence buffer fade (Rauschen-style)
//   2. Stutter wrap ghosts (spawn on loop wrap, fade by age)
//   3. Tap dots, projected through Z-displacement + rotation
//   4. Ricochet flash (per-tap brightness boost, decays per block)
//   5. Listener marker (cross at orbit position)
//
// Header-only inline per feedback_no_out_of_line_virtuals — vtable
// must be COMDAT-linked.

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
      mRotAngle = 0.0f;
      mFrameCounter = 0;
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

    // Project a 3D point through current rotation/tilt to a 2D pixel.
    // Returns rotated z (depth) for optional brightness modulation.
    inline void project3D(float x, float y, float z,
                          int *outPx, int *outPy, float *outDepth) const
    {
      const float cosA = cosf(mRotAngle);
      const float sinA = sinf(mRotAngle);
      const float costilt = 0.9553f;   // cos(0.3)
      const float sintilt = 0.2955f;   // sin(0.3)
      // Rotate around Y axis (turntable tumble).
      const float rx    = x * cosA + z * sinA;
      const float rzNew = -x * sinA + z * cosA;
      const float ry    = y;
      // Tilt around X axis for 2.5D feel.
      const float fx = rx;
      const float fy = ry * costilt - rzNew * sintilt;
      const float depth = rzNew * costilt + ry * sintilt;
      // Project to ply (centered, scale ~26 to leave margin for the
      // listener at radius 1.3 + ghost spawns slightly outside taps).
      const int w = mWidth < kMaxW ? mWidth : kMaxW;
      const int h = mHeight < kMaxH ? mHeight : kMaxH;
      *outPx = (int)(fx * 22.0f) + w / 2;
      *outPy = (int)(fy * 22.0f) + h / 2;
      *outDepth = depth;
    }

    // Z displacement per glitch mode + state.
    inline float tapZ(int t) const
    {
      const int mode = mpNetwork->getTapMode(t);
      switch (mode)
      {
        case NETWORK_TAP_NORMAL:
          return 0.0f;
        case NETWORK_TAP_MUTE:
          return -1.0f;
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
          // kScrubMaxFrac = 0.25 in Network DSP; normalize to that.
          const float scrubMaxSamples = 0.25f * (float)maxD;
          if (scrubMaxSamples < 1.0f) return 0.0f;
          float norm = (float)offset / scrubMaxSamples;
          if (norm > 1.0f) norm = 1.0f;
          if (norm < -1.0f) norm = -1.0f;
          return 0.4f * norm;
        }
        case NETWORK_TAP_REVERSE:
          return -0.4f;
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

      // ---- 1. Fade persistence buffer ----
      for (int i = 0; i < w * h; i++)
      {
        if (mPixels[i] > 0)
          mPixels[i]--;
      }

      // ---- 2. Detect stutter wrap; spawn ghost in persistence ----
      const int activeCount = mpNetwork->getActiveTapCount();
      for (int t = 0; t < activeCount; t++)
      {
        const int curIter = mpNetwork->getTapStutterIter(t);
        const int lastIter = (int)mLastStutterIter[t];
        // Wrap detected when iterations decremented (loop end).
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
          project3D(gx, gy, 0.0f, &px, &py, &pz);
          if (px >= 0 && px < w && py >= 0 && py < h)
          {
            mPixels[py * w + px] = 12;
          }
        }
        mLastStutterIter[t] = (uint8_t)curIter;
      }

      // ---- 3. Advance rotation ----
      mRotAngle += 0.01f;
      if (mRotAngle > 6.2832f) mRotAngle -= 6.2832f;

      // Clear framebuffer region, then render layers.
      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);

      // ---- Render persistence (ghost trails) ----
      for (int y = 0; y < h; y++)
      {
        for (int x = 0; x < w; x++)
        {
          const uint8_t v = mPixels[y * w + x];
          if (v > 0)
            fb.pixel(v, mWorldLeft + x, mWorldBottom + y);
        }
      }

      // ---- Render tap dots ----
      const int flashMax = mpNetwork->getRicochetFlashMax();
      for (int t = 0; t < activeCount; t++)
      {
        const float rx = mpNetwork->getReflectorX(t);
        const float ry = mpNetwork->getReflectorY(t);
        const float rz = tapZ(t);
        int px, py;
        float pz;
        project3D(rx, ry, rz, &px, &py, &pz);
        if (px < 0 || px >= w || py < 0 || py >= h) continue;

        const int mode = mpNetwork->getTapMode(t);
        int brightness;
        bool hollow = false;
        switch (mode)
        {
          case NETWORK_TAP_MUTE:
            brightness = 3;
            break;
          case NETWORK_TAP_CRUSH:
          {
            uint32_t hh = (uint32_t)t * 2654435761u +
                          (uint32_t)mFrameCounter;
            hh = hh * 1103515245u + 12345u;
            brightness = ((hh >> 16) & 1u) ? 12 : 6;
            break;
          }
          case NETWORK_TAP_REVERSE:
            brightness = 10;
            hollow = true;
            break;
          case NETWORK_TAP_STUTTER:
          case NETWORK_TAP_SCRUB:
          case NETWORK_TAP_NORMAL:
          default:
            brightness = 12;
            break;
        }

        // Ricochet flash boost.
        const int flash = mpNetwork->getRicochetFlash(t);
        if (flash > 0 && flashMax > 0)
        {
          brightness += (flash * 6) / flashMax;
          if (brightness > 15) brightness = 15;
        }

        if (hollow)
        {
          // 3x3 outline (hollow center).
          for (int dy = -1; dy <= 1; dy++)
          {
            for (int dx = -1; dx <= 1; dx++)
            {
              if (dx == 0 && dy == 0) continue;
              const int rpx = px + dx;
              const int rpy = py + dy;
              if (rpx >= 0 && rpx < w && rpy >= 0 && rpy < h)
              {
                fb.pixel(brightness,
                         mWorldLeft + rpx, mWorldBottom + rpy);
              }
            }
          }
        }
        else
        {
          fb.pixel(brightness, mWorldLeft + px, mWorldBottom + py);
        }
      }

      // ---- Listener marker (cross) ----
      const float listenerPhase = mpNetwork->getListenerPhase();
      const float lx = cosf(2.0f * 3.14159265f * listenerPhase) * 1.3f;
      const float ly = sinf(2.0f * 3.14159265f * listenerPhase) * 1.3f;
      int lpx, lpy;
      float lpz;
      project3D(lx, ly, 0.0f, &lpx, &lpy, &lpz);
      if (lpx >= 1 && lpx < w - 1 && lpy >= 1 && lpy < h - 1)
      {
        fb.pixel(WHITE, mWorldLeft + lpx,     mWorldBottom + lpy);
        fb.pixel(10,    mWorldLeft + lpx + 1, mWorldBottom + lpy);
        fb.pixel(10,    mWorldLeft + lpx - 1, mWorldBottom + lpy);
        fb.pixel(10,    mWorldLeft + lpx,     mWorldBottom + lpy + 1);
        fb.pixel(10,    mWorldLeft + lpx,     mWorldBottom + lpy - 1);
      }
    }
#endif
  };
} // namespace stolmine
