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

  class RaindropGraphic : public od::Graphic
  {
  public:
    RaindropGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height)
    {
      memset(mField, 0, sizeof(mField));
      mTime = 0.0f;
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

    // --- Perlin noise (2D, classic gradient) ---

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

    // Fractal Brownian motion: layered Perlin for richer texture
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

    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpDelay)
        return;

      int tapCount = mpDelay->getTapCount();
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

      // Advance time (scrolls field downward)
      mTime += 0.02f * timeMul;

      // Noise scale: lower = larger blobs
      float noiseScale = 0.06f;
      // Threshold for contour extraction
      float threshold = 0.12f - feedback * 0.1f; // higher base, feedback lowers it

      // --- Phase 1: Evaluate noise field with tap modulation ---
      for (int gy = 0; gy < gh; gy++)
      {
        float ny = (float)gy * noiseScale + mTime;
        for (int gx = 0; gx < gw; gx++)
        {
          float nx = (float)gx * noiseScale;

          // Base noise: 3-octave fBm scrolling downward
          float val = fbm(nx, ny, 3);

          // Tap modulation: each active tap boosts the field at its x position
          for (int t = 0; t < tapCount; t++)
          {
            float tapTime = mpDelay->getTapTime(t);
            float energy = mpDelay->getTapEnergy(t);
            float level = mpDelay->getTapLevel(t);

            if (energy < 0.001f && level < 0.01f) continue;

            // Tap x position in grid space
            float tapX = 2.0f + tapTime * (float)(gw - 4);
            float dx = (float)gx - tapX;

            // Random jitter: per-tap noise contribution that varies over time
            float tapNoise = perlin((float)t * 7.3f + mTime * 0.5f,
                                    (float)gy * 0.08f + mTime * 0.3f) * 0.15f;

            // Gaussian boost centered on tap x, full height
            float sigma = 3.0f + energy * 4.0f;
            float boost = (energy * 0.6f + level * 0.15f + tapNoise)
                        * expf(-dx * dx / (2.0f * sigma * sigma));
            if (boost > 0.0f) val += boost;
          }

          // Store as fixed-point 0-255
          float normalized = (val + 0.5f);
          int ival = (int)(normalized * 200.0f);
          if (ival < 0) ival = 0;
          if (ival > 255) ival = 255;

          // Slew: ~200ms at 55fps = ~11 frames, coeff ~0.09
          int idx = gy * gw + gx;
          int prev = (int)mField[idx];
          int slewed = prev + (int)((float)(ival - prev) * 0.09f);
          if (slewed < 0) slewed = 0;
          if (slewed > 255) slewed = 255;
          mField[idx] = (uint8_t)slewed;
        }
      }

      // Convert threshold to field-space
      int fieldThresh = (int)((threshold + 0.5f) * 200.0f);
      if (fieldThresh < 1) fieldThresh = 1;
      if (fieldThresh > 250) fieldThresh = 250;

      // --- Phase 2: Marching squares contour extraction ---
      int edgeBand = 15; // pixels within this range of threshold get contour lines
      for (int cy = 0; cy < gh - 1; cy++)
      {
        for (int cx = 0; cx < gw - 1; cx++)
        {
          // Four corners of this cell
          int v00 = mField[cy * gw + cx];
          int v10 = mField[cy * gw + cx + 1];
          int v01 = mField[(cy + 1) * gw + cx];
          int v11 = mField[(cy + 1) * gw + cx + 1];

          // Classify corners (inside = 1, outside = 0)
          int config = 0;
          if (v00 >= fieldThresh) config |= 1;
          if (v10 >= fieldThresh) config |= 2;
          if (v01 >= fieldThresh) config |= 4;
          if (v11 >= fieldThresh) config |= 8;

          // Skip fully inside (15) or fully outside (0)
          if (config == 0 || config == 15) continue;

          // Linear interpolation along edges to find contour crossing points
          // Edge 0: bottom (v00 -> v10), y = cy
          // Edge 1: right  (v10 -> v11), x = cx+1
          // Edge 2: top    (v01 -> v11), y = cy+1
          // Edge 3: left   (v00 -> v01), x = cx

          float ex[4], ey[4];
          bool eActive[4] = {false, false, false, false};

          // Bottom edge
          if ((v00 >= fieldThresh) != (v10 >= fieldThresh))
          {
            float t = (float)(fieldThresh - v00) / (float)(v10 - v00);
            ex[0] = (float)cx + t;
            ey[0] = (float)cy;
            eActive[0] = true;
          }
          // Right edge
          if ((v10 >= fieldThresh) != (v11 >= fieldThresh))
          {
            float t = (float)(fieldThresh - v10) / (float)(v11 - v10);
            ex[1] = (float)(cx + 1);
            ey[1] = (float)cy + t;
            eActive[1] = true;
          }
          // Top edge
          if ((v01 >= fieldThresh) != (v11 >= fieldThresh))
          {
            float t = (float)(fieldThresh - v01) / (float)(v11 - v01);
            ex[2] = (float)cx + t;
            ey[2] = (float)(cy + 1);
            eActive[2] = true;
          }
          // Left edge
          if ((v00 >= fieldThresh) != (v01 >= fieldThresh))
          {
            float t = (float)(fieldThresh - v00) / (float)(v01 - v00);
            ex[3] = (float)cx;
            ey[3] = (float)cy + t;
            eActive[3] = true;
          }

          // Draw contour segments based on marching squares lookup
          // Each boundary cell produces 1 or 2 line segments
          static const int kSegTable[16][4] = {
            {-1,-1,-1,-1}, // 0: no edges
            { 0, 3,-1,-1}, // 1
            { 0, 1,-1,-1}, // 2
            { 1, 3,-1,-1}, // 3
            { 2, 3,-1,-1}, // 4
            { 0, 1, 2, 3}, // 5: ambiguous -- two segments
            { 0, 2,-1,-1}, // 6
            { 1, 2,-1,-1}, // 7
            { 1, 2,-1,-1}, // 8
            { 0, 2,-1,-1}, // 9
            { 0, 3, 1, 2}, // 10: ambiguous -- two segments
            { 1, 2,-1,-1}, // 11 (duplicate of 7 intentional for symmetry)
            { 1, 3,-1,-1}, // 12
            { 0, 1,-1,-1}, // 13
            { 0, 3,-1,-1}, // 14
            {-1,-1,-1,-1}, // 15: no edges
          };

          // Corrected segment table
          const int *segs = kSegTable[config];

          // Draw first segment
          if (segs[0] >= 0 && segs[1] >= 0 && eActive[segs[0]] && eActive[segs[1]])
          {
            int x0 = left + (int)(ex[segs[0]] + 0.5f);
            int y0 = bot + (int)(ey[segs[0]] + 0.5f);
            int x1 = left + (int)(ex[segs[1]] + 0.5f);
            int y1 = bot + (int)(ey[segs[1]] + 0.5f);
            fb.line(WHITE, x0, y0, x1, y1);
          }
          // Draw second segment (ambiguous cases 5 and 10)
          if (segs[2] >= 0 && segs[3] >= 0 && eActive[segs[2]] && eActive[segs[3]])
          {
            int x0 = left + (int)(ex[segs[2]] + 0.5f);
            int y0 = bot + (int)(ey[segs[2]] + 0.5f);
            int x1 = left + (int)(ex[segs[3]] + 0.5f);
            int y1 = bot + (int)(ey[segs[3]] + 0.5f);
            fb.line(WHITE, x0, y0, x1, y1);
          }
        }
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
    int mPerm[512]; // Perlin permutation table
    uint8_t mField[kRainGridW * kRainGridH];
  };

} // namespace stolmine
