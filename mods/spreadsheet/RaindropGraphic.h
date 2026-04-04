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
  static const int kNumThresholds = 3;
  static const int kNoiseLUTSize = 64;
  static const float kNoiseLUTInv = 1.0f / (float)kNoiseLUTSize;

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

      // Initialize permutation table (used only for LUT bake)
      int perm[512];
      for (int i = 0; i < 256; i++)
        perm[i] = i;
      for (int i = 255; i > 0; i--)
      {
        int j = od::Random::generateInteger(0, i);
        int tmp = perm[i];
        perm[i] = perm[j];
        perm[j] = tmp;
      }
      for (int i = 0; i < 256; i++)
        perm[i + 256] = perm[i];

      // Bake tileable noise LUT using Perlin
      bakeNoiseLUT(perm);
    }

    virtual ~RaindropGraphic()
    {
      if (mpDelay)
        mpDelay->release();
    }

#ifndef SWIGLUA

    // --- Perlin helpers (used only at init for LUT bake) ---

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

    static float perlinEval(const int *perm, float x, float y)
    {
      int xi = (int)floorf(x) & 255;
      int yi = (int)floorf(y) & 255;
      float xf = x - floorf(x);
      float yf = y - floorf(y);
      float u = fade(xf);
      float v = fade(yf);

      int aa = perm[perm[xi] + yi];
      int ab = perm[perm[xi] + yi + 1];
      int ba = perm[perm[xi + 1] + yi];
      int bb = perm[perm[xi + 1] + yi + 1];

      return lerp(
          lerp(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u),
          lerp(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u),
          v);
    }

    void bakeNoiseLUT(const int *perm)
    {
      // Fill LUT with Perlin noise, 4 periods across so it tiles at LUT boundaries
      for (int y = 0; y < kNoiseLUTSize; y++)
      {
        float ny = (float)y * (4.0f * kNoiseLUTInv);
        for (int x = 0; x < kNoiseLUTSize; x++)
        {
          float nx = (float)x * (4.0f * kNoiseLUTInv);
          mNoiseLUT[y * kNoiseLUTSize + x] = perlinEval(perm, nx, ny);
        }
      }
    }

    // --- Runtime LUT sampling (replaces per-frame Perlin) ---

    float sampleNoise(float u, float v) const
    {
      // Wrap to [0,1) and scale to LUT coords
      u = u - floorf(u);
      v = v - floorf(v);
      float fx = u * (float)kNoiseLUTSize;
      float fy = v * (float)kNoiseLUTSize;
      int x0 = (int)fx;
      int y0 = (int)fy;
      float sx = fx - (float)x0;
      float sy = fy - (float)y0;
      x0 &= (kNoiseLUTSize - 1);
      y0 &= (kNoiseLUTSize - 1);
      int x1 = (x0 + 1) & (kNoiseLUTSize - 1);
      int y1 = (y0 + 1) & (kNoiseLUTSize - 1);

      float v00 = mNoiseLUT[y0 * kNoiseLUTSize + x0];
      float v10 = mNoiseLUT[y0 * kNoiseLUTSize + x1];
      float v01 = mNoiseLUT[y1 * kNoiseLUTSize + x0];
      float v11 = mNoiseLUT[y1 * kNoiseLUTSize + x1];

      return lerp(lerp(v00, v10, sx), lerp(v01, v11, sx), sy);
    }

    float fbmLUT(float u, float v) const
    {
      float val = sampleNoise(u, v) * 0.5f;
      val += sampleNoise(u * 2.0f, v * 2.0f) * 0.25f;
      val += sampleNoise(u * 4.0f, v * 4.0f) * 0.125f;
      return val;
    }

    // --- Tap spot management ---

    void assignSpotPosition(int i, int gw, int gh)
    {
      mSpots[i].x = (float)od::Random::generateInteger(2, gw - 3);
      mSpots[i].y = (float)od::Random::generateInteger(2, gh - 3);
      mSpots[i].energy = 0.0f;
      mSpots[i].assigned = true;
    }

    // --- Marching squares contour drawing ---

    // Single-pass multi-threshold marching squares: reads each cell once
    void drawContoursMulti(od::FrameBuffer &fb, int left, int bot,
                           int gw, int gh, float scaleX, float scaleY,
                           const int *thresholds, const int *colors, int numLevels)
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
        int rowOff = cy * gw;
        int rowOff1 = rowOff + gw;
        for (int cx = 0; cx < gw - 1; cx++)
        {
          // Read 4 corners once
          int v00 = mSlewedField[rowOff + cx];
          int v10 = mSlewedField[rowOff + cx + 1];
          int v01 = mSlewedField[rowOff1 + cx];
          int v11 = mSlewedField[rowOff1 + cx + 1];

          // Quick reject: find cell min/max to skip cells with no crossings
          int cmin = v00, cmax = v00;
          if (v10 < cmin) cmin = v10; if (v10 > cmax) cmax = v10;
          if (v01 < cmin) cmin = v01; if (v01 > cmax) cmax = v01;
          if (v11 < cmin) cmin = v11; if (v11 > cmax) cmax = v11;

          // Check each threshold level
          for (int lvl = 0; lvl < numLevels; lvl++)
          {
            int thresh = thresholds[lvl];

            // Skip if all corners on same side of this threshold
            if (cmin >= thresh || cmax < thresh)
              continue;

            int config = 0;
            if (v00 >= thresh) config |= 1;
            if (v10 >= thresh) config |= 2;
            if (v01 >= thresh) config |= 4;
            if (v11 >= thresh) config |= 8;

            if (config == 0 || config == 15)
              continue;

            const int *segs = kSegTable[config];

            // Compute edge interpolation points
            float ex[4], ey[4];

            // Edge 0: top (v00-v10)
            if (segs[0] == 0 || segs[1] == 0 || segs[2] == 0 || segs[3] == 0)
            {
              float t = (float)(thresh - v00) / (float)(v10 - v00);
              ex[0] = (float)cx + t;
              ey[0] = (float)cy;
            }
            // Edge 1: right (v10-v11)
            if (segs[0] == 1 || segs[1] == 1 || segs[2] == 1 || segs[3] == 1)
            {
              float t = (float)(thresh - v10) / (float)(v11 - v10);
              ex[1] = (float)(cx + 1);
              ey[1] = (float)cy + t;
            }
            // Edge 2: bottom (v01-v11)
            if (segs[0] == 2 || segs[1] == 2 || segs[2] == 2 || segs[3] == 2)
            {
              float t = (float)(thresh - v01) / (float)(v11 - v01);
              ex[2] = (float)cx + t;
              ey[2] = (float)(cy + 1);
            }
            // Edge 3: left (v00-v01)
            if (segs[0] == 3 || segs[1] == 3 || segs[2] == 3 || segs[3] == 3)
            {
              float t = (float)(thresh - v00) / (float)(v01 - v00);
              ex[3] = (float)cx;
              ey[3] = (float)cy + t;
            }

            int color = colors[lvl];

            if (segs[0] >= 0 && segs[1] >= 0)
            {
              fb.line(color,
                      left + (int)(ex[segs[0]] * scaleX + 0.5f),
                      bot + (int)(ey[segs[0]] * scaleY + 0.5f),
                      left + (int)(ex[segs[1]] * scaleX + 0.5f),
                      bot + (int)(ey[segs[1]] * scaleY + 0.5f));
            }
            if (segs[2] >= 0 && segs[3] >= 0)
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
    }

    // --- Main draw ---

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
      float scaleX = (float)w / (float)(gw - 1);
      float scaleY = (float)h / (float)(gh - 1);

      // --- 1. Read DSP state ---
      int tapCount = mpDelay->getTapCount();
      float feedback = mpDelay->mFeedback.value();
      float masterTime = mpDelay->mMasterTime.value();
      float voctPitch = mpDelay->mVOctPitch.value() * 10.0f;
      float timeMul = powf(2.0f, voctPitch);
      if (timeMul < 0.1f) timeMul = 0.1f;
      if (timeMul > 8.0f) timeMul = 8.0f;

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

      // --- 4. Noise field via LUT sampling ---
      float timeNorm = (masterTime - 0.01f) / 19.99f;
      if (timeNorm < 0.0f) timeNorm = 0.0f;
      if (timeNorm > 1.0f) timeNorm = 1.0f;
      // Noise spatial frequency: short delay = fine, long = broad
      // Range 0.15..0.45 keeps the base octave well within one LUT tile
      float noiseFreq = 0.15f + (1.0f - timeNorm) * 0.30f;
      float warpStrength = 0.12f;
      float bumpRadius = 8.0f / sqrtf((float)tapCount);
      float r2 = bumpRadius * bumpRadius;
      float r2x4 = r2 * 4.0f;
      float invGw = 1.0f / (float)gw;
      float invGh = 1.0f / (float)gh;

      for (int gy = 0; gy < gh; gy++)
      {
        // Normalize grid to [0, noiseFreq] + time scroll
        float baseY = (float)gy * invGh * noiseFreq + mTime;
        for (int gx = 0; gx < gw; gx++)
        {
          float baseX = (float)gx * invGw * noiseFreq;

          // Domain warp via LUT (decorrelated by UV offset)
          float wx = baseX + mAggEnergy * sampleNoise(baseX * 2.0f, baseY * 2.0f + 3.7f) * warpStrength;
          float wy = baseY + mAggEnergy * sampleNoise(baseX * 2.0f + 7.3f, baseY * 2.0f) * warpStrength;

          // FBM via LUT (3 octaves)
          float val = fbmLUT(wx, wy);

          // Tap energy bumps
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

      // --- 5. Slew field ---
      for (int i = 0; i < gw * gh; i++)
      {
        float current = (float)mSlewedField[i];
        current += ((float)mField[i] - current) * slewCoeff;
        mSlewedField[i] = (uint8_t)(current + 0.5f);
      }

      // --- 6. Marching squares contours ---
      int fieldMin = 255, fieldMax = 0;
      for (int i = 0; i < gw * gh; i++)
      {
        if (mSlewedField[i] < fieldMin) fieldMin = mSlewedField[i];
        if (mSlewedField[i] > fieldMax) fieldMax = mSlewedField[i];
      }
      int fieldRange = fieldMax - fieldMin;
      if (fieldRange < 10) fieldRange = 10;

      // Pre-compute thresholds and draw all passes
      int thresholds[kNumThresholds];
      int colors[kNumThresholds];
      for (int lvl = 0; lvl < kNumThresholds; lvl++)
      {
        thresholds[lvl] = fieldMin + (fieldRange * (lvl + 1)) / (kNumThresholds + 1);
        int brightness = WHITE - lvl * 3;
        colors[lvl] = brightness < 3 ? 3 : brightness;
      }

      // Single pass over grid, all thresholds at once
      drawContoursMulti(fb, left, bot, gw, gh, scaleX, scaleY,
                        thresholds, colors, kNumThresholds);
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
    float mNoiseLUT[kNoiseLUTSize * kNoiseLUTSize]; // 16KB tileable noise
    uint8_t mField[kRainGridW * kRainGridH];
    uint8_t mSlewedField[kRainGridW * kRainGridH];
    TapSpot mSpots[kMaxTaps];
    float mAggEnergy;
    int mLastTapCount;
  };

} // namespace stolmine
