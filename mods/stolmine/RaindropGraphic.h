#pragma once

#include <od/graphics/Graphic.h>
#include <od/extras/Random.h>
#include <MultitapDelay.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static const int kMaxDrops = 32;
  static const int kMaxBeads = 48;
  static const int kMaxSplash = 12;
  static const int kTrailLen = 8;
  static const int kRainGridW = 42;
  static const int kRainGridH = 64;

  // Metaball field thresholds
  static const int kEdgeLo = 35;
  static const int kEdgeHi = 65;

  class RaindropGraphic : public od::Graphic
  {
  public:
    RaindropGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height)
    {
      memset(mFieldGrid, 0, sizeof(mFieldGrid));
      memset(mSpawnAccum, 0, sizeof(mSpawnAccum));
      memset(mTapWasActive, 0, sizeof(mTapWasActive));
      for (int i = 0; i < kMaxDrops; i++)
        mDrops[i].active = false;
      for (int i = 0; i < kMaxBeads; i++)
        mBeads[i].active = false;
      for (int i = 0; i < kMaxSplash; i++)
        mSplashes[i].active = false;
      mFrameCount = 0;
    }

    virtual ~RaindropGraphic()
    {
      if (mpDelay)
        mpDelay->release();
    }

#ifndef SWIGLUA

    // Sawtooth envelope: slow buildup (85%), fast slide (15%)
    static float saw(float t)
    {
      float b = 0.85f;
      float up = t / b;
      if (up < 0.0f) up = 0.0f;
      if (up > 1.0f) up = 1.0f;
      up = up * up * (3.0f - 2.0f * up);

      float dn = (1.0f - t) / (1.0f - b);
      if (dn < 0.0f) dn = 0.0f;
      if (dn > 1.0f) dn = 1.0f;
      dn = dn * dn * (3.0f - 2.0f * dn);

      return up * dn;
    }

    // FM wobble: sin(y + sin(y))
    static float fmWobble(float y)
    {
      return sinf(y + sinf(y));
    }

    // Splat a metaball into the field grid
    // Quadratic bump: strength * max(0, 1 - d^2/R^2)^2
    void splatBall(float cx, float cy, float R, float strength)
    {
      int gw = kRainGridW;
      int gh = kRainGridH;
      float invR2 = 1.0f / (R * R);
      int ix0 = (int)(cx - R);
      int ix1 = (int)(cx + R) + 1;
      int iy0 = (int)(cy - R);
      int iy1 = (int)(cy + R) + 1;
      if (ix0 < 0) ix0 = 0;
      if (ix1 >= gw) ix1 = gw - 1;
      if (iy0 < 0) iy0 = 0;
      if (iy1 >= gh) iy1 = gh - 1;

      for (int iy = iy0; iy <= iy1; iy++)
      {
        float dy = (float)iy - cy;
        float dy2 = dy * dy;
        for (int ix = ix0; ix <= ix1; ix++)
        {
          float dx = (float)ix - cx;
          float d2 = dx * dx + dy2;
          float t = 1.0f - d2 * invR2;
          if (t <= 0.0f) continue;
          t = t * t;
          int val = (int)(strength * t);
          int idx = iy * gw + ix;
          int sum = (int)mFieldGrid[idx] + val;
          mFieldGrid[idx] = (sum > 255) ? 255 : (uint8_t)sum;
        }
      }
    }

    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpDelay)
        return;

      int tapCount = mpDelay->getTapCount();
      if (tapCount < 1)
        return;

      int left = mWorldLeft;
      int bot = mWorldBottom;
      int w = mWidth;
      int h = mHeight;
      int gw = (w < kRainGridW) ? w : kRainGridW;
      int gh = (h < kRainGridH) ? h : kRainGridH;

      float feedback = mpDelay->mFeedback.value();
      float voctPitch = mpDelay->mVOctPitch.value() * 10.0f;
      float timeMul = powf(2.0f, voctPitch);
      if (timeMul < 0.1f) timeMul = 0.1f;
      if (timeMul > 8.0f) timeMul = 8.0f;

      mFrameCount++;

      // --- Phase 1: Decay/clear field grid ---
      if (feedback < 0.01f)
      {
        memset(mFieldGrid, 0, gw * gh);
      }
      else
      {
        // Multiply-decay: high feedback = wet persistence on glass
        int decay = 256 - (int)(feedback * 180.0f); // 256 (full clear) to 76 (70% retain)
        for (int i = 0; i < gw * gh; i++)
          mFieldGrid[i] = (uint8_t)((int)mFieldGrid[i] * decay >> 8);
      }

      // --- Fade beads ---
      for (int i = 0; i < kMaxBeads; i++)
      {
        if (!mBeads[i].active) continue;
        mBeads[i].life--;
        if (mBeads[i].life <= 0)
          mBeads[i].active = false;
      }

      // --- Update splashes ---
      for (int i = 0; i < kMaxSplash; i++)
      {
        if (!mSplashes[i].active) continue;
        mSplashes[i].x += mSplashes[i].vx;
        mSplashes[i].strength *= 0.8f;
        mSplashes[i].life--;
        if (mSplashes[i].life <= 0)
          mSplashes[i].active = false;
      }

      // --- Phase 2: Spawn and update drops ---
      int marginX = 2;
      int fieldW = gw - 2 * marginX;
      if (fieldW < 4) fieldW = 4;

      for (int t = 0; t < tapCount; t++)
      {
        float energy = mpDelay->getTapEnergy(t);

        bool energyHigh = energy > 0.01f;
        bool wasHigh = mTapWasActive[t];
        mTapWasActive[t] = energyHigh;

        bool spawn = (!wasHigh && energyHigh);
        if (energyHigh && !spawn)
        {
          mSpawnAccum[t] += energy * 0.02f * timeMul;
          if (mSpawnAccum[t] >= 1.0f)
          {
            mSpawnAccum[t] -= 1.0f;
            spawn = true;
          }
        }

        if (spawn)
        {
          int slot = -1;
          for (int i = 0; i < kMaxDrops; i++)
          {
            if (!mDrops[i].active) { slot = i; break; }
          }
          if (slot < 0) continue;

          Drop &d = mDrops[slot];
          d.active = true;
          d.tapIndex = t;

          float tapTime = mpDelay->getTapTime(t);
          float tapLevel = mpDelay->getTapLevel(t);
          float tapPan = mpDelay->getTapPan(t);
          float tapPitch = mpDelay->getTapPitch(t);

          float baseX = (float)marginX + tapTime * (float)fieldW;
          d.spawnX = baseX;
          d.x = baseX + od::Random::generateFloat(-0.5f, 0.5f);
          d.y = (float)(gh - 1);

          d.sawPhase = od::Random::generateFloat(0.0f, 1.0f);
          float pitchSpeed = powf(2.0f, tapPitch / 24.0f);
          d.sawSpeed = (0.006f + od::Random::generateFloat(0.0f, 0.004f))
                     * pitchSpeed * timeMul;

          d.wobbleSeed = od::Random::generateFloat(0.0f, 100.0f);
          d.wobbleAmp = od::Random::generateFloat(0.6f, 1.5f);
          d.wobbleDrift = tapPan * 0.08f;

          d.brightness = 4 + (int)(tapLevel * 10.0f);
          if (d.brightness > 14) d.brightness = 14;

          d.lastSawY = 0.0f;
          d.mass = 1.0f;
          d.trailHead = 0;
          d.trailCount = 0;
        }
      }

      // Update drops
      for (int i = 0; i < kMaxDrops; i++)
      {
        Drop &d = mDrops[i];
        if (!d.active) continue;

        // Sawtooth motion
        d.sawPhase += d.sawSpeed;
        if (d.sawPhase > 1.0f) d.sawPhase -= 1.0f;

        float sawY = saw(d.sawPhase);
        float deltaY = sawY - d.lastSawY;
        if (deltaY < -0.3f) deltaY += 1.0f;
        d.lastSawY = sawY;

        float fallPixels = deltaY * 25.0f * d.mass;
        d.y -= fallPixels;

        // FM wobble
        float normY = d.y * 0.15f + d.wobbleSeed;
        float wx = fmWobble(normY) * d.wobbleAmp;
        float distFromSpawn = d.x - d.spawnX;
        float edgeDamp = 1.0f - fabsf(distFromSpawn) * 0.3f;
        if (edgeDamp < 0.2f) edgeDamp = 0.2f;
        wx *= edgeDamp;
        d.x += (wx * 0.15f + d.wobbleDrift) * (0.5f + fallPixels);

        int ix = (int)(d.x + 0.5f);
        int iy = (int)(d.y + 0.5f);

        // Bead absorption
        for (int b = 0; b < kMaxBeads; b++)
        {
          if (!mBeads[b].active) continue;
          int dx = ix - mBeads[b].x;
          int dy = iy - mBeads[b].y;
          if (dx * dx + dy * dy <= 3)
          {
            d.mass = 1.5f;
            int boost = d.brightness + 1;
            if (boost <= 15) d.brightness = boost;
            mBeads[b].active = false;
          }
        }

        if (d.mass > 1.0f)
        {
          d.mass -= 0.03f;
          if (d.mass < 1.0f) d.mass = 1.0f;
        }

        // Record trail position
        d.trailX[d.trailHead] = d.x;
        d.trailY[d.trailHead] = d.y;
        d.trailHead = (d.trailHead + 1) % kTrailLen;
        if (d.trailCount < kTrailLen) d.trailCount++;

        // Trail beads: periodic placement
        float normalizedY = d.y / (float)gh;
        float beadWave = sinf(normalizedY * (1.0f - normalizedY) * 80.0f);
        if (beadWave > 0.7f && ix >= 0 && ix < gw && iy >= 0 && iy < gh)
        {
          for (int b = 0; b < kMaxBeads; b++)
          {
            if (!mBeads[b].active)
            {
              bool tooClose = false;
              for (int c = 0; c < kMaxBeads; c++)
              {
                if (!mBeads[c].active || c == b) continue;
                int cdx = ix - mBeads[c].x;
                int cdy = iy - mBeads[c].y;
                if (cdx * cdx + cdy * cdy <= 4) { tooClose = true; break; }
              }
              if (tooClose) break;

              mBeads[b].active = true;
              mBeads[b].x = ix;
              mBeads[b].y = iy;
              mBeads[b].brightness = d.brightness / 2;
              if (mBeads[b].brightness < 2) mBeads[b].brightness = 2;
              mBeads[b].life = 25 + (int)(feedback * 50.0f);
              break;
            }
          }
        }

        // Drop landed
        if (d.y < 0.0f)
        {
          d.active = false;

          // Spawn splash particles
          if (feedback > 0.1f)
          {
            int nSplash = 2 + (int)(feedback * 2.0f);
            for (int s = 0; s < nSplash; s++)
            {
              for (int si = 0; si < kMaxSplash; si++)
              {
                if (!mSplashes[si].active)
                {
                  mSplashes[si].active = true;
                  mSplashes[si].x = d.x + od::Random::generateFloat(-1.0f, 1.0f);
                  mSplashes[si].y = 1.0f;
                  mSplashes[si].vx = od::Random::generateFloat(-0.8f, 0.8f) * feedback;
                  mSplashes[si].strength = 60.0f + (float)d.brightness * 4.0f;
                  mSplashes[si].life = 4 + (int)(feedback * 6.0f);
                  break;
                }
              }
            }
          }
        }
      }

      // --- Phase 3: Splat metaballs into field grid ---

      // Splat drop heads + trails
      for (int i = 0; i < kMaxDrops; i++)
      {
        Drop &d = mDrops[i];
        if (!d.active) continue;

        // Head ball: radius from mass and level
        float headR = 1.5f + d.mass * 1.5f;
        float headStr = 120.0f + (float)d.brightness * 8.0f;
        splatBall(d.x, d.y, headR, headStr);

        // Trail balls: decay with age
        float trailDecay = 1.0f - feedback * 0.4f; // high feedback = slower decay
        for (int t = 0; t < d.trailCount; t++)
        {
          int idx = (d.trailHead - 1 - t + kTrailLen) % kTrailLen;
          float age = (float)(t + 1) / (float)(d.trailCount + 1);
          float tR = headR * (0.8f - 0.4f * age);
          if (tR < 0.8f) tR = 0.8f;
          float tStr = headStr * 0.5f * (1.0f - age * trailDecay);
          if (tStr < 5.0f) continue;
          splatBall(d.trailX[idx], d.trailY[idx], tR, tStr);
        }
      }

      // Splat beads
      for (int i = 0; i < kMaxBeads; i++)
      {
        if (!mBeads[i].active) continue;
        float fadeMul = (mBeads[i].life < 10)
            ? (float)mBeads[i].life / 10.0f : 1.0f;
        float bStr = (float)mBeads[i].brightness * 6.0f * fadeMul;
        if (bStr < 3.0f) continue;
        splatBall((float)mBeads[i].x, (float)mBeads[i].y, 1.2f, bStr);
      }

      // Splat splashes
      for (int i = 0; i < kMaxSplash; i++)
      {
        if (!mSplashes[i].active) continue;
        splatBall(mSplashes[i].x, mSplashes[i].y, 1.5f, mSplashes[i].strength);
      }

      // --- Phase 4: Render field to framebuffer ---
      for (int gy = 0; gy < gh; gy++)
      {
        for (int gx = 0; gx < gw; gx++)
        {
          int val = mFieldGrid[gy * gw + gx];
          if (val < kEdgeLo) continue;

          int color;
          if (val >= kEdgeHi)
          {
            // Interior fill: dimmer (water body through glass)
            color = 2 + (int)((float)(val - kEdgeHi) / (255.0f - (float)kEdgeHi) * 8.0f);
            if (color > 10) color = 10;
          }
          else
          {
            // Edge band: bright highlight (surface tension / refraction)
            float edgeT = (float)(val - kEdgeLo) / (float)(kEdgeHi - kEdgeLo);
            float peak = 1.0f - fabsf(2.0f * edgeT - 1.0f);
            color = 8 + (int)(peak * 7.0f);
          }
          fb.pixel(color, left + gx, bot + gy);
        }
      }

      // --- Phase 5: Drop head specular highlights ---
      for (int i = 0; i < kMaxDrops; i++)
      {
        Drop &d = mDrops[i];
        if (!d.active) continue;

        int hx = (int)(d.x + 0.5f);
        int hy = (int)(d.y + 0.5f);
        if (hx >= 0 && hx < gw && hy >= 0 && hy < gh)
          fb.pixel(WHITE, left + hx, bot + hy);
      }
    }
#endif

    void follow(MultitapDelay *pDelay)
    {
      if (mpDelay)
        mpDelay->release();
      mpDelay = pDelay;
      if (mpDelay)
        mpDelay->attach();
    }

    void setSelectedTap(int tap) { mSelectedTap = tap; }

  private:
    struct Drop
    {
      float x, y;
      float spawnX;
      float sawPhase, sawSpeed;
      float lastSawY;
      float wobbleSeed, wobbleAmp;
      float wobbleDrift;
      float mass;
      int brightness;
      int tapIndex;
      bool active;
      // Trail ring buffer
      float trailX[kTrailLen];
      float trailY[kTrailLen];
      uint8_t trailHead;
      uint8_t trailCount;
    };

    struct Bead
    {
      int x, y;
      int brightness;
      int life;
      bool active;
    };

    struct Splash
    {
      float x, y;
      float vx;
      float strength;
      uint8_t life;
      bool active;
    };

    MultitapDelay *mpDelay = 0;
    int mSelectedTap = -1;
    int mFrameCount = 0;
    Drop mDrops[kMaxDrops];
    Bead mBeads[kMaxBeads];
    Splash mSplashes[kMaxSplash];
    uint8_t mFieldGrid[kRainGridW * kRainGridH];
    float mSpawnAccum[kMaxTaps];
    bool mTapWasActive[kMaxTaps];
  };

} // namespace stolmine
