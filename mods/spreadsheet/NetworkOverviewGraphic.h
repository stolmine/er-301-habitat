#pragma once

// Network overview viz — 3D phase-space disc + geometry overlay.
//
// Reflects the Network unit's geometry, glitch state, and audio
// content on the gltch ply. Layered render (back-to-front):
//
//   1. Phase-space points from wet bus output ring (Rauschen
//      pattern: 254 points, (s[i], s[i+1], s[i+2]) as 3D coord).
//      Auto-scaled, additive into persistence buffer.
//   2. Stutter wrap ghosts (per-loop-wrap spawn into persistence).
//   3. Persistence buffer rendered (fades 1/frame).
//   4. Listener trajectory trace (last 128 walker positions,
//      brightness fades by age).
//   5. Tap dots (size-scaled disc, mode-encoded Z displacement).
//   6. Listener marker (cross at current position).
//
// Size knob contracts/expands the disc (scale = sizeNorm × base).
// Phase-space layer stays fixed-scale (auto-normalized to its own
// dynamic range).
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
      mScaleMin = -1.0f;
      mScaleMax = 1.0f;
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

    // Phase-space auto-scale (smoothed min/max with fast expand,
    // slow contract — matches Rauschen).
    float mScaleMin;
    float mScaleMax;

    // Project a 3D point through current rotation/tilt to a 2D
    // pixel at the given scale (in pixels per unit).
    inline void project3D(float x, float y, float z, float scale,
                          int *outPx, int *outPy, float *outDepth) const
    {
      const float cosA = cosf(mRotAngle);
      const float sinA = sinf(mRotAngle);
      const float costilt = 0.9553f;   // cos(0.3)
      const float sintilt = 0.2955f;   // sin(0.3)
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

      // Disc render scale (geometry layer) follows the Size knob;
      // phase-space layer stays at fixed unit-disc scale.
      const float sizeNorm = mpNetwork->getSizeNorm();
      const float kBaseScale = 26.0f;
      const float geomScale = sizeNorm * kBaseScale;
      const float phaseScale = kBaseScale;

      // ---- 1. Fade persistence buffer ----
      for (int i = 0; i < w * h; i++)
      {
        if (mPixels[i] > 0)
          mPixels[i]--;
      }

      // ---- 2. Phase-space auto-scale ----
      const int ringSize = mpNetwork->getOutputRingSize();
      float curMin = 1e10f, curMax = -1e10f;
      for (int i = 0; i < ringSize; i++)
      {
        const float s = mpNetwork->getOutputSample(i);
        if (!(s == s) || s > 1e6f || s < -1e6f) continue;
        if (s < curMin) curMin = s;
        if (s > curMax) curMax = s;
      }
      if (curMin > curMax) { curMin = -1.0f; curMax = 1.0f; }
      const float expandRate = 0.5f;
      const float contractRate = 0.02f;
      if (curMin < mScaleMin)
        mScaleMin += (curMin - mScaleMin) * expandRate;
      else
        mScaleMin += (curMin - mScaleMin) * contractRate;
      if (curMax > mScaleMax)
        mScaleMax += (curMax - mScaleMax) * expandRate;
      else
        mScaleMax += (curMax - mScaleMax) * contractRate;
      if (!(mScaleMin == mScaleMin)) mScaleMin = -1.0f;
      if (!(mScaleMax == mScaleMax)) mScaleMax = 1.0f;
      if (mScaleMax <= mScaleMin) mScaleMax = mScaleMin + 0.01f;
      float range = mScaleMax - mScaleMin;
      if (range < 0.01f)
      {
        const float mid = (mScaleMin + mScaleMax) * 0.5f;
        mScaleMin = mid - 0.005f;
        mScaleMax = mid + 0.005f;
        range = 0.01f;
      }
      const float invRange = 1.0f / range;

      // ---- 3. Plot phase-space points (additive persistence) ----
      for (int i = 0; i < ringSize - 2; i++)
      {
        const float s0 = mpNetwork->getOutputSample(i);
        const float s1 = mpNetwork->getOutputSample(i + 1);
        const float s2 = mpNetwork->getOutputSample(i + 2);
        if (!(s0 == s0) || !(s1 == s1) || !(s2 == s2)) continue;
        // Normalize to [-0.5, 0.5] in each axis.
        const float nx = (s0 - mScaleMin) * invRange - 0.5f;
        const float ny = (s1 - mScaleMin) * invRange - 0.5f;
        const float nz = (s2 - mScaleMin) * invRange - 0.5f;
        int px, py;
        float pz;
        project3D(nx, ny, nz, phaseScale, &px, &py, &pz);
        if (px >= 0 && px < w && py >= 0 && py < h)
        {
          int b = (int)mPixels[py * w + px] + 4;
          if (b > 12) b = 12;
          mPixels[py * w + px] = (uint8_t)b;
        }
      }

      // ---- 4. Stutter wrap → ghost spawn into persistence ----
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

      // ---- 5. Advance rotation ----
      mRotAngle += 0.01f;
      if (mRotAngle > 6.2832f) mRotAngle -= 6.2832f;

      // Clear framebuffer region.
      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);

      // ---- 6. Render persistence (phase space + ghosts) ----
      for (int y = 0; y < h; y++)
      {
        for (int x = 0; x < w; x++)
        {
          const uint8_t v = mPixels[y * w + x];
          if (v > 0)
            fb.pixel(v, mWorldLeft + x, mWorldBottom + y);
        }
      }

      // ---- 7. Listener trajectory trace (fading by age) ----
      const int traceSize = mpNetwork->getListenerTraceSize();
      const float kListenerR = 1.3f;
      for (int i = 0; i < traceSize; i++)
      {
        const float phase = mpNetwork->getListenerTracePhase(i);
        const float tx =
          cosf(2.0f * 3.14159265f * phase) * kListenerR;
        const float ty =
          sinf(2.0f * 3.14159265f * phase) * kListenerR;
        int px, py;
        float pz;
        project3D(tx, ty, 0.0f, geomScale, &px, &py, &pz);
        if (px < 0 || px >= w || py < 0 || py >= h) continue;
        // 0 = oldest, traceSize-1 = newest. Brightness 3..11.
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
          case NETWORK_TAP_MUTE:
            brightness = 3;
            break;
          case NETWORK_TAP_CRUSH:
          {
            uint32_t hh = (uint32_t)t * 2654435761u +
                          (uint32_t)mFrameCounter;
            hh = hh * 1103515245u + 12345u;
            brightness = ((hh >> 16) & 1u) ? 13 : 7;
            break;
          }
          case NETWORK_TAP_REVERSE:
            brightness = 11;
            hollow = true;
            break;
          case NETWORK_TAP_STUTTER:
          case NETWORK_TAP_SCRUB:
          case NETWORK_TAP_NORMAL:
          default:
            brightness = 13;
            break;
        }

        const int flash = mpNetwork->getRicochetFlash(t);
        if (flash > 0 && flashMax > 0)
        {
          brightness += (flash * 6) / flashMax;
          if (brightness > 15) brightness = 15;
        }

        if (hollow)
        {
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

      // ---- 9. Listener marker (current position) ----
      const float listenerPhase = mpNetwork->getListenerPhase();
      const float lx = cosf(2.0f * 3.14159265f * listenerPhase) * kListenerR;
      const float ly = sinf(2.0f * 3.14159265f * listenerPhase) * kListenerR;
      int lpx, lpy;
      float lpz;
      project3D(lx, ly, 0.0f, geomScale, &lpx, &lpy, &lpz);
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
