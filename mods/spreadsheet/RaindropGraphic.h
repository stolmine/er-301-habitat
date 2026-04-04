#pragma once

#include <od/graphics/Graphic.h>
#include <od/extras/Random.h>
#include <MultitapDelay.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static const int kRainGridW = 36;
  static const int kRainGridH = 54;
  static const int kNumThresholds = 5;
  static const int kPerlinInterval = 1;

  struct TapSpot
  {
    float x, y;
    float energy;
    bool assigned;
  };

  class RaindropGraphic : public od::Graphic
  {
  public:
    RaindropGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height)
    {
      memset(mBaseField, 0, sizeof(mBaseField));
      memset(mField, 0, sizeof(mField));
      memset(mSlewedField, 0, sizeof(mSlewedField));
      mTime = 0.0f;
      mAggEnergy = 0.0f;
      mLastTapCount = 0;
      mFrameCounter = 0;

      for (int i = 0; i < kMaxTaps; i++)
      {
        mSpots[i].x = 0.0f;
        mSpots[i].y = 0.0f;
        mSpots[i].energy = 0.0f;
        mSpots[i].assigned = false;
      }

      // Initialize permutation table
      for (int i = 0; i < 256; i++)
        mPerm[i] = i;
      for (int i = 255; i > 0; i--)
      {
        int j = od::Random::generateInteger(0, i);
        int tmp = mPerm[i];
        mPerm[i] = mPerm[j];
        mPerm[j] = tmp;
      }
      for (int i = 0; i < 256; i++)
        mPerm[i + 256] = mPerm[i];
    }

    virtual ~RaindropGraphic()
    {
      if (mpDelay)
        mpDelay->release();
    }

#ifndef SWIGLUA

    static float fade(float t)
    {
      return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    static float lerp(float a, float b, float t)
    {
      return a + t * (b - a);
    }

    static float grad(int hash, float x, float y)
    {
      int h = hash & 7;
      float u = h < 4 ? x : y;
      float v = h < 4 ? y : x;
      return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }

    float perlin(float x, float y)
    {
      int xi = (int)floorf(x) & 255;
      int yi = (int)floorf(y) & 255;
      float xf = x - floorf(x);
      float yf = y - floorf(y);
      float u = fade(xf);
      float v = fade(yf);

      int aa = mPerm[mPerm[xi] + yi];
      int ab = mPerm[mPerm[xi] + yi + 1];
      int ba = mPerm[mPerm[xi + 1] + yi];
      int bb = mPerm[mPerm[xi + 1] + yi + 1];

      return lerp(
          lerp(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u),
          lerp(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u),
          v);
    }

    float fbm(float x, float y)
    {
      float val = perlin(x, y) * 0.5f;
      val += perlin(x * 2.0f, y * 2.0f) * 0.25f;
      val += perlin(x * 4.0f, y * 4.0f) * 0.125f;
      return val;
    }

    void assignSpotPosition(int i, int gw, int gh)
    {
      mSpots[i].x = (float)od::Random::generateInteger(2, gw - 3);
      mSpots[i].y = (float)od::Random::generateInteger(2, gh - 3);
      mSpots[i].energy = 0.0f;
      mSpots[i].assigned = true;
    }

    void drawContourPass(od::FrameBuffer &fb, int left, int bot,
                         int gw, int gh, float scaleX, float scaleY,
                         int fieldThresh, int color)
    {
      static const int kSegTable[16][4] = {
          {-1, -1, -1, -1},
          {0, 3, -1, -1},
          {0, 1, -1, -1},
          {1, 3, -1, -1},
          {2, 3, -1, -1},
          {0, 1, 2, 3},
          {0, 2, -1, -1},
          {1, 2, -1, -1},
          {1, 2, -1, -1},
          {0, 2, -1, -1},
          {0, 3, 1, 2},
          {1, 2, -1, -1},
          {1, 3, -1, -1},
          {0, 1, -1, -1},
          {0, 3, -1, -1},
          {-1, -1, -1, -1},
      };

      for (int cy = 0; cy < gh - 1; cy++)
      {
        for (int cx = 0; cx < gw - 1; cx++)
        {
          int v00 = mSlewedField[cy * gw + cx];
          int v10 = mSlewedField[cy * gw + cx + 1];
          int v01 = mSlewedField[(cy + 1) * gw + cx];
          int v11 = mSlewedField[(cy + 1) * gw + cx + 1];

          int config = 0;
          if (v00 >= fieldThresh)
            config |= 1;
          if (v10 >= fieldThresh)
            config |= 2;
          if (v01 >= fieldThresh)
            config |= 4;
          if (v11 >= fieldThresh)
            config |= 8;

          if (config == 0 || config == 15)
            continue;

          float ex[4], ey[4];
          bool eActive[4] = {false, false, false, false};

          if ((v00 >= fieldThresh) != (v10 >= fieldThresh))
          {
            float t = (float)(fieldThresh - v00) / (float)(v10 - v00);
            ex[0] = (float)cx + t;
            ey[0] = (float)cy;
            eActive[0] = true;
          }
          if ((v10 >= fieldThresh) != (v11 >= fieldThresh))
          {
            float t = (float)(fieldThresh - v10) / (float)(v11 - v10);
            ex[1] = (float)(cx + 1);
            ey[1] = (float)cy + t;
            eActive[1] = true;
          }
          if ((v01 >= fieldThresh) != (v11 >= fieldThresh))
          {
            float t = (float)(fieldThresh - v01) / (float)(v11 - v01);
            ex[2] = (float)cx + t;
            ey[2] = (float)(cy + 1);
            eActive[2] = true;
          }
          if ((v00 >= fieldThresh) != (v01 >= fieldThresh))
          {
            float t = (float)(fieldThresh - v00) / (float)(v01 - v00);
            ex[3] = (float)cx;
            ey[3] = (float)cy + t;
            eActive[3] = true;
          }

          const int *segs = kSegTable[config];

          if (segs[0] >= 0 && segs[1] >= 0 && eActive[segs[0]] && eActive[segs[1]])
          {
            fb.line(color,
                    left + (int)(ex[segs[0]] * scaleX + 0.5f),
                    bot + (int)(ey[segs[0]] * scaleY + 0.5f),
                    left + (int)(ex[segs[1]] * scaleX + 0.5f),
                    bot + (int)(ey[segs[1]] * scaleY + 0.5f));
          }
          if (segs[2] >= 0 && segs[3] >= 0 && eActive[segs[2]] && eActive[segs[3]])
          {
            fb.line(color,
                    left + (int)(ex[segs[2]] * scaleX + 0.5f),
                    bot + (int)(ey[segs[2]] * scaleY + 0.5f),
                    left + (int)(ex[segs[3]] * scaleX + 0.5f),
                    bot + (int)(ey[segs[3]] * scaleY + 0.5f));
          }
        }
      }
    }

    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpDelay)
        return;

      int left = mWorldLeft;
      int bot = mWorldBottom;
      int w = mWidth;
      int h = mHeight;
      int gw = kRainGridW;
      int gh = kRainGridH;
      // Scale factors to map grid coords to screen pixels
      float scaleX = (float)w / (float)(gw - 1);
      float scaleY = (float)h / (float)(gh - 1);

      // --- 1. Read DSP state ---
      int tapCount = mpDelay->getTapCount();
      float feedback = mpDelay->mFeedback.value();
      float masterTime = mpDelay->mMasterTime.value();
      float voctPitch = mpDelay->mVOctPitch.value() * 10.0f;
      float timeMul = powf(2.0f, voctPitch);
      if (timeMul < 0.1f)
        timeMul = 0.1f;
      if (timeMul > 8.0f)
        timeMul = 8.0f;

      mTime += 0.02f * timeMul;

      // --- 2. Feedback-controlled slew ---
      float slewMs = 150.0f + feedback * 850.0f;
      float slewCoeff = 1.0f / (1.0f + slewMs * 0.055f);

      // --- 3. Update tap spots ---
      for (int i = tapCount; i < kMaxTaps; i++)
      {
        if (mSpots[i].assigned && mSpots[i].energy < 0.001f)
          mSpots[i].assigned = false;
      }
      for (int i = mLastTapCount; i < tapCount; i++)
      {
        if (!mSpots[i].assigned)
          assignSpotPosition(i, gw, gh);
      }
      mLastTapCount = tapCount;

      // Slew spot energies
      float rawAgg = 0.0f;
      for (int i = 0; i < kMaxTaps; i++)
      {
        if (!mSpots[i].assigned)
          continue;
        float target = (i < tapCount) ? mpDelay->getTapEnergy(i) : 0.0f;
        mSpots[i].energy += (target - mSpots[i].energy) * slewCoeff;
        rawAgg += mSpots[i].energy;
      }
      mAggEnergy += (rawAgg - mAggEnergy) * slewCoeff;

      // --- 4. SLOW PATH: Perlin base field (every Nth frame) ---
      mFrameCounter++;
      if (mFrameCounter >= kPerlinInterval)
      {
        mFrameCounter = 0;

        float timeNorm = (masterTime - 0.01f) / 19.99f;
        if (timeNorm < 0.0f) timeNorm = 0.0f;
        if (timeNorm > 1.0f) timeNorm = 1.0f;
        float noiseScale = 0.04f + (1.0f - timeNorm) * 0.08f;
        float warpStrength = 3.0f;

        for (int gy = 0; gy < gh; gy++)
        {
          float baseY = (float)gy * noiseScale + mTime;
          for (int gx = 0; gx < gw; gx++)
          {
            float baseX = (float)gx * noiseScale;

            // Domain warp
            float wx = baseX + mAggEnergy * perlin(baseX * 2.0f, baseY * 2.0f + 100.0f) * warpStrength;
            float wy = baseY + mAggEnergy * perlin(baseX * 2.0f + 50.0f, baseY * 2.0f) * warpStrength;

            mBaseField[gy * gw + gx] = fbm(wx, wy);
          }
        }
      }

      // --- 5. FAST PATH: composite base + tap bumps (every frame) ---
      float bumpRadius = 8.0f / sqrtf((float)tapCount);
      float r2 = bumpRadius * bumpRadius;
      float r2x4 = r2 * 4.0f;

      for (int gy = 0; gy < gh; gy++)
      {
        for (int gx = 0; gx < gw; gx++)
        {
          float val = mBaseField[gy * gw + gx];

          for (int t = 0; t < tapCount; t++)
          {
            if (!mSpots[t].assigned || mSpots[t].energy < 0.001f)
              continue;
            float dx = (float)gx - mSpots[t].x;
            float dy = (float)gy - mSpots[t].y;
            float dist2 = dx * dx + dy * dy;
            if (dist2 < r2x4)
            {
              float u = dist2 / r2;
              float g = 1.0f / (1.0f + u);
              val += mSpots[t].energy * g * g;
            }
          }

          int ival = (int)((val + 0.5f) * 255.0f);
          if (ival < 0) ival = 0;
          if (ival > 255) ival = 255;
          mField[gy * gw + gx] = (uint8_t)ival;
        }
      }

      // --- 6. Slew field ---
      for (int i = 0; i < gw * gh; i++)
      {
        float current = (float)mSlewedField[i];
        current += ((float)mField[i] - current) * slewCoeff;
        mSlewedField[i] = (uint8_t)(current + 0.5f);
      }

      // --- 7. Marching squares contours ---
      int fieldMin = 255, fieldMax = 0;
      for (int i = 0; i < gw * gh; i++)
      {
        if (mSlewedField[i] < fieldMin) fieldMin = mSlewedField[i];
        if (mSlewedField[i] > fieldMax) fieldMax = mSlewedField[i];
      }
      int fieldRange = fieldMax - fieldMin;
      if (fieldRange < 10) fieldRange = 10;

      for (int lvl = 0; lvl < kNumThresholds; lvl++)
      {
        int thresh = fieldMin + (fieldRange * (lvl + 1)) / (kNumThresholds + 1);
        int brightness = WHITE - lvl * 3;
        if (brightness < 3) brightness = 3;
        drawContourPass(fb, left, bot, gw, gh, scaleX, scaleY, thresh, brightness);
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
    MultitapDelay *mpDelay = 0;
    int mSelectedTap = -1;
    float mTime;
    int mFrameCounter;
    int mPerm[512];
    float mBaseField[kRainGridW * kRainGridH]; // Perlin values (slow update)
    uint8_t mField[kRainGridW * kRainGridH];   // composite (base + bumps)
    uint8_t mSlewedField[kRainGridW * kRainGridH];
    TapSpot mSpots[kMaxTaps];
    float mAggEnergy;
    int mLastTapCount;
  };

} // namespace stolmine
