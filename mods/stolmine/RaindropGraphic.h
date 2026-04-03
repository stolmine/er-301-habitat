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

      // Feedback -> slew rate: 50ms (feedback=0) to 350ms (feedback=1)
      float slewMs = 50.0f + feedback * 300.0f;
      float slewFrames = slewMs * 0.055f;
      float slewCoeff = 1.0f / (1.0f + slewFrames);

      // Noise scale: lower = larger blobs
      float noiseScale = 0.06f;
      // Threshold for contour extraction
      float threshold = 0.12f - feedback * 0.1f;

      // Aggregate energy: sum of all tap energies, drives topo fill opacity
      float aggEnergy = 0.0f;
      for (int t = 0; t < tapCount; t++)
        aggEnergy += mpDelay->getTapEnergy(t);
      if (tapCount > 0)
        aggEnergy /= (float)tapCount;
      if (aggEnergy > 1.0f) aggEnergy = 1.0f;
      // Slew aggregate energy for smooth transitions
      mAggEnergy += (aggEnergy - mAggEnergy) * slewCoeff;

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

            // Tap x position in grid space
            float tapX = 2.0f + tapTime * (float)(gw - 4);
            float dx = (float)gx - tapX;

            // Energy boost (skip if no signal)
            if (energy < 0.001f && level < 0.01f) continue;

            // Random jitter
            float tapNoise = perlin((float)t * 7.3f + mTime * 0.5f,
                                    (float)gy * 0.08f + mTime * 0.3f) * 0.15f;

            // Gaussian boost centered on tap x
            float sigma = 3.0f + energy * 4.0f;
            float boost = (energy * 0.6f + level * 0.15f + tapNoise)
                        * expf(-dx * dx / (2.0f * sigma * sigma));
            if (boost > 0.0f) val += boost;
          }

          // Store as fixed-point 0-255 (full range)
          float normalized = (val + 0.5f);
          int ival = (int)(normalized * 255.0f);
          if (ival < 0) ival = 0;
          if (ival > 255) ival = 255;

          // Slew: feedback-controlled (50-350ms)
          int idx = gy * gw + gx;
          int prev = (int)mField[idx];
          int slewed = prev + (int)((float)(ival - prev) * slewCoeff);
          if (slewed < 0) slewed = 0;
          if (slewed > 255) slewed = 255;
          mField[idx] = (uint8_t)slewed;
        }
      }

      // Convert threshold to field-space
      int fieldThresh = (int)((threshold + 0.5f) * 255.0f);
      if (fieldThresh < 1) fieldThresh = 1;
      if (fieldThresh > 250) fieldThresh = 250;

      // Pre-cache tap x positions for contour brightness
      float tapXPos[kMaxTaps];
      float tapEn[kMaxTaps];
      for (int t = 0; t < tapCount; t++)
      {
        tapXPos[t] = 2.0f + mpDelay->getTapTime(t) * (float)(gw - 4);
        tapEn[t] = mpDelay->getTapEnergy(t);
      }

      // --- Phase 2: Figure/ground crossfade (mix-controlled, slewed) ---
      float mix = mpDelay->mMix.value();
      if (mix < 0.0f) mix = 0.0f;
      if (mix > 1.0f) mix = 1.0f;
      // Slew mix at half the feedback rate
      float mixSlewCoeff = 1.0f / (1.0f + slewMs * 0.5f * 0.055f);
      mMixSlewed += (mix - mMixSlewed) * mixSlewCoeff;
      mix = mMixSlewed;

      // Brightness: energy-modulated (quiet = moderate, loud = full)
      float brightnessScale = 0.5f + mAggEnergy * 0.5f;

      // Find actual field range for proper normalization
      int fieldMin = 255, fieldMax = 0;
      for (int i = 0; i < gw * gh; i++)
      {
        if (mField[i] < fieldMin) fieldMin = mField[i];
        if (mField[i] > fieldMax) fieldMax = mField[i];
      }
      float fieldRange = (float)(fieldMax - fieldMin);
      if (fieldRange < 1.0f) fieldRange = 1.0f;

      // Contour count (slewed, used by both fill drain and marching squares)
      float targetContours = 1.0f + mAggEnergy * 5.0f;
      if (targetContours > 6.0f) targetContours = 6.0f;
      mContourCount += (targetContours - mContourCount) * slewCoeff;
      int maxContour = (int)(mContourCount + 0.5f);
      if (maxContour < 1) maxContour = 1;
      if (maxContour > 6) maxContour = 6;
      float fractional = mContourCount - floorf(mContourCount);
      int contourRange = fieldMax - fieldThresh;
      if (contourRange < 1) contourRange = 1;

      // Fill with contour drain: lines suck brightness from surrounding fill
      // contourDrain: 0 = flat fill, 1 = all brightness pulled into contour lines
      float contourDrain = mAggEnergy * 0.9f;
      // Spacing between contour levels in field-value units
      float drainSpacing = (maxContour > 1)
          ? (float)contourRange / (float)maxContour
          : (float)contourRange;
      if (drainSpacing < 1.0f) drainSpacing = 1.0f;

      for (int gy = 0; gy < gh; gy++)
      {
        for (int gx = 0; gx < gw; gx++)
        {
          int val = mField[gy * gw + gx];
          bool inside = val >= fieldThresh;

          float norm = (float)(val - fieldMin) / fieldRange;
          float baseBright;
          if (inside)
            baseBright = norm * 10.0f * brightnessScale;
          else
            baseBright = (1.0f - norm) * 10.0f * brightnessScale;

          // Distance to nearest contour level (in field-value space)
          float fval = (float)(val - fieldThresh);
          float nearest = drainSpacing;
          for (int ci = 0; ci < maxContour; ci++)
          {
            float contourVal = (float)(ci * contourRange) / (float)maxContour;
            float d = fabsf(fval - contourVal);
            if (d < nearest) nearest = d;
          }

          // Drain: pixels near contour lines lose brightness
          // proximity 1.0 = on a contour line, 0.0 = far from any
          float halfSpacing = drainSpacing * 0.5f;
          float proximity = 1.0f - nearest / halfSpacing;
          if (proximity < 0.0f) proximity = 0.0f;

          // Drain factor: energy controls how much brightness the lines absorb
          // At drain=0: fillMul=1 (flat fill). At drain=1: fillMul near 0 close to lines
          float fillMul = 1.0f - proximity * contourDrain;
          if (fillMul < 0.05f) fillMul = 0.05f;

          float bright = baseBright * fillMul;

          // Crossfade figure/ground
          float figBright = inside ? bright : 0.0f;
          float gndBright = inside ? 0.0f : bright;
          int color = (int)(figBright * (1.0f - mix) + gndBright * mix);
          if (color > 12) color = 12;
          if (color > 0)
            fb.pixel(color, left + gx, bot + gy);
        }
      }

      // --- Phase 3: Multi-threshold marching squares ---
      static const int kMaxContours = 6;
      int thresholds[kMaxContours];
      int colors[kMaxContours];
      for (int ci = 0; ci < maxContour; ci++)
      {
        thresholds[ci] = fieldThresh + (ci * contourRange) / maxContour;
        // Base brightness: outer bright, inner dimmer
        int bright = 15 - (ci * 10) / maxContour;
        if (bright < 4) bright = 4;
        // Fade the outermost active contour in/out smoothly
        if (ci == maxContour - 1 && maxContour > 1)
        {
          bright = (int)((float)bright * fractional);
          if (bright < 1) bright = 1;
        }
        colors[ci] = bright;
      }
      int activeContours = maxContour;

      // Segment lookup table
      static const int kSegTable[16][4] = {
        {-1,-1,-1,-1}, // 0
        { 0, 3,-1,-1}, // 1
        { 0, 1,-1,-1}, // 2
        { 1, 3,-1,-1}, // 3
        { 2, 3,-1,-1}, // 4
        { 0, 1, 2, 3}, // 5: ambiguous
        { 0, 2,-1,-1}, // 6
        { 1, 2,-1,-1}, // 7
        { 1, 2,-1,-1}, // 8
        { 0, 2,-1,-1}, // 9
        { 0, 3, 1, 2}, // 10: ambiguous
        { 1, 2,-1,-1}, // 11
        { 1, 3,-1,-1}, // 12
        { 0, 1,-1,-1}, // 13
        { 0, 3,-1,-1}, // 14
        {-1,-1,-1,-1}, // 15
      };

      for (int ci = activeContours - 1; ci >= 0; ci--)
      {
        int thresh = thresholds[ci];
        if (thresh > 254 || thresh < 1) continue;
        int baseColor = colors[ci];

        for (int cy = 0; cy < gh - 1; cy++)
        {
          for (int cx = 0; cx < gw - 1; cx++)
          {
            int v00 = mField[cy * gw + cx];
            int v10 = mField[cy * gw + cx + 1];
            int v01 = mField[(cy + 1) * gw + cx];
            int v11 = mField[(cy + 1) * gw + cx + 1];

            int config = 0;
            if (v00 >= thresh) config |= 1;
            if (v10 >= thresh) config |= 2;
            if (v01 >= thresh) config |= 4;
            if (v11 >= thresh) config |= 8;

            if (config == 0 || config == 15) continue;

            float ex[4], ey[4];
            bool eActive[4] = {false, false, false, false};

            if ((v00 >= thresh) != (v10 >= thresh))
            {
              float t = (float)(thresh - v00) / (float)(v10 - v00);
              ex[0] = (float)cx + t;
              ey[0] = (float)cy;
              eActive[0] = true;
            }
            if ((v10 >= thresh) != (v11 >= thresh))
            {
              float t = (float)(thresh - v10) / (float)(v11 - v10);
              ex[1] = (float)(cx + 1);
              ey[1] = (float)cy + t;
              eActive[1] = true;
            }
            if ((v01 >= thresh) != (v11 >= thresh))
            {
              float t = (float)(thresh - v01) / (float)(v11 - v01);
              ex[2] = (float)cx + t;
              ey[2] = (float)(cy + 1);
              eActive[2] = true;
            }
            if ((v00 >= thresh) != (v01 >= thresh))
            {
              float t = (float)(thresh - v00) / (float)(v01 - v00);
              ex[3] = (float)cx;
              ey[3] = (float)cy + t;
              eActive[3] = true;
            }

            const int *segs = kSegTable[config];

            // Compute tap-proximity brightness for this cell
            float midX = (float)cx + 0.5f;
            int lineColor = baseColor;
            if (tapCount > 0)
            {
              float bestEnergy = 0.0f;
              for (int t = 0; t < tapCount; t++)
              {
                float d = fabsf(midX - tapXPos[t]);
                float influence = tapEn[t] / (1.0f + d * 0.3f);
                if (influence > bestEnergy) bestEnergy = influence;
              }
              // Boost brightness near active taps
              int boost = (int)(bestEnergy * 6.0f);
              lineColor = baseColor + boost;
              if (lineColor > 15) lineColor = 15;
            }

            // Contour lines always bright -- structural skeleton visible at any mix

            if (segs[0] >= 0 && segs[1] >= 0 && eActive[segs[0]] && eActive[segs[1]])
            {
              int x0 = left + (int)(ex[segs[0]] + 0.5f);
              int y0 = bot + (int)(ey[segs[0]] + 0.5f);
              int x1 = left + (int)(ex[segs[1]] + 0.5f);
              int y1 = bot + (int)(ey[segs[1]] + 0.5f);
              fb.line(lineColor, x0, y0, x1, y1);
            }
            if (segs[2] >= 0 && segs[3] >= 0 && eActive[segs[2]] && eActive[segs[3]])
            {
              int x0 = left + (int)(ex[segs[2]] + 0.5f);
              int y0 = bot + (int)(ey[segs[2]] + 0.5f);
              int x1 = left + (int)(ex[segs[3]] + 0.5f);
              int y1 = bot + (int)(ey[segs[3]] + 0.5f);
              fb.line(lineColor, x0, y0, x1, y1);
            }
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
    float mAggEnergy = 0.0f;
    float mMixSlewed = 0.5f;
    float mContourCount = 1.0f;
    int mPerm[512]; // Perlin permutation table
    uint8_t mField[kRainGridW * kRainGridH];
  };

} // namespace stolmine
