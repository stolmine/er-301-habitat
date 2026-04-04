#pragma once

#include <od/graphics/Graphic.h>
#include <od/extras/Random.h>
#include <MultitapDelay.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static const int kRainGridW = 42;
  static const int kRainGridH = 64;
  static const int kNumThresholds = 5;

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
      memset(mField, 0, sizeof(mField));
      memset(mSlewedField, 0, sizeof(mSlewedField));
      mTime = 0.0f;
      mAggEnergy = 0.0f;
      mLastTapCount = 0;

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

    float fbm(float x, float y, int octaves)
    {
      float val = 0.0f;
      float amp = 0.5f;
      float freq = 1.0f;
      for (int i = 0; i < octaves; i++)
      {
        val += perlin(x * freq, y * freq) * amp;
        freq *= 2.0f;
        amp *= 0.5f;
      }
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
                         int gw, int gh, int fieldThresh, int color)
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
            fb.line(color, left + (int)(ex[segs[0]] + 0.5f), bot + (int)(ey[segs[0]] + 0.5f),
                    left + (int)(ex[segs[1]] + 0.5f), bot + (int)(ey[segs[1]] + 0.5f));
          }
          if (segs[2] >= 0 && segs[3] >= 0 && eActive[segs[2]] && eActive[segs[3]])
          {
            fb.line(color, left + (int)(ex[segs[2]] + 0.5f), bot + (int)(ey[segs[2]] + 0.5f),
                    left + (int)(ex[segs[3]] + 0.5f), bot + (int)(ey[segs[3]] + 0.5f));
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
      int gw = (w < kRainGridW) ? w : kRainGridW;
      int gh = (h < kRainGridH) ? h : kRainGridH;

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
      float slewMs = 150.0f + feedback * 300.0f;
      float slewCoeff = 1.0f / (1.0f + slewMs * 0.055f);

      // --- 3. Update tap spots ---
      // Deactivate removed taps
      for (int i = tapCount; i < kMaxTaps; i++)
      {
        if (mSpots[i].assigned && mSpots[i].energy < 0.001f)
          mSpots[i].assigned = false;
      }
      // Activate new taps
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

      // --- 4. Aggregate energy ---
      mAggEnergy += (rawAgg - mAggEnergy) * slewCoeff;

      // --- 5 & 6. Generate base Perlin field with domain warp ---
      // Master time controls noise spatial scale
      // Normalize masterTime (0.01-4.0) to 0-1, invert: short delay = fine, long = broad
      float timeNorm = (masterTime - 0.01f) / 3.99f;
      if (timeNorm < 0.0f) timeNorm = 0.0f;
      if (timeNorm > 1.0f) timeNorm = 1.0f;
      float noiseScale = 0.04f + (1.0f - timeNorm) * 0.08f;
      float warpStrength = 3.0f;
      // Bump radius scales with tap count
      float bumpRadius = 8.0f / sqrtf((float)tapCount);

      for (int gy = 0; gy < gh; gy++)
      {
        float baseY = (float)gy * noiseScale + mTime;
        for (int gx = 0; gx < gw; gx++)
        {
          float baseX = (float)gx * noiseScale;

          // Domain warp: aggregate energy distorts coordinates
          float wx = baseX + mAggEnergy * perlin(baseX * 2.0f, baseY * 2.0f + 100.0f) * warpStrength;
          float wy = baseY + mAggEnergy * perlin(baseX * 2.0f + 50.0f, baseY * 2.0f) * warpStrength;

          float val = fbm(wx, wy, 3);

          // --- 7. Add Gaussian bumps from tap spots ---
          for (int t = 0; t < kMaxTaps; t++)
          {
            if (!mSpots[t].assigned || mSpots[t].energy < 0.001f)
              continue;
            float dx = (float)gx - mSpots[t].x;
            float dy = (float)gy - mSpots[t].y;
            float dist2 = dx * dx + dy * dy;
            float r2 = bumpRadius * bumpRadius;
            if (dist2 < r2 * 4.0f)
              val += mSpots[t].energy * expf(-dist2 / r2);
          }

          int ival = (int)((val + 0.5f) * 255.0f);
          if (ival < 0)
            ival = 0;
          if (ival > 255)
            ival = 255;
          mField[gy * gw + gx] = (uint8_t)ival;
        }
      }

      // --- 8. Slew field toward target ---
      for (int i = 0; i < gw * gh; i++)
      {
        float target = (float)mField[i];
        float current = (float)mSlewedField[i];
        current += (target - current) * slewCoeff;
        mSlewedField[i] = (uint8_t)(current + 0.5f);
      }

      // --- 9. Adaptive multi-threshold marching squares ---
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
        // Space thresholds evenly across actual field range
        int thresh = fieldMin + (fieldRange * (lvl + 1)) / (kNumThresholds + 1);

        int brightness = WHITE - lvl * 3;
        if (brightness < 3)
          brightness = 3;

        drawContourPass(fb, left, bot, gw, gh, thresh, brightness);
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
    int mPerm[512];
    uint8_t mField[kRainGridW * kRainGridH];
    uint8_t mSlewedField[kRainGridW * kRainGridH];
    TapSpot mSpots[kMaxTaps];
    float mAggEnergy;
    int mLastTapCount;
  };

} // namespace stolmine
