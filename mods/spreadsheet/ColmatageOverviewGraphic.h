#pragma once

#include <od/graphics/Graphic.h>
#include "Colmatage.h"
#include <string.h>
#include <math.h>

namespace stolmine
{

  class ColmatageOverviewGraphic : public od::Graphic
  {
  public:
    ColmatageOverviewGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height)
    {
      memset(mCells, 0, sizeof(mCells));
      mNumCells = 0;
      mTime = 0.0f;
      mLastPhrasePos = -1;
      mLastCut = -1;
      mRng = 31415;
      bakeNoiseLUT();
    }

    virtual ~ColmatageOverviewGraphic()
    {
      if (mpColmatage)
        mpColmatage->release();
    }

    void follow(Colmatage *p)
    {
      if (mpColmatage)
        mpColmatage->release();
      mpColmatage = p;
      if (mpColmatage)
        mpColmatage->attach();
    }

#ifndef SWIGLUA

    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpColmatage)
        return;

      int w = mWidth;
      int h = mHeight;
      int left = mWorldLeft;
      int bot = mWorldBottom;

      int pos = mpColmatage->getPhrasePosition();
      if (pos < mLastPhrasePos || mNumCells == 0)
      {
        float bs = mpColmatage->getBlockSize();
        repartition(w, h, bs);
      }
      mLastPhrasePos = pos;

      mTime += 0.015f;

      int curCut = mpColmatage->getCurrentCut();
      if (curCut != mLastCut)
      {
        if (mNumCells > 0)
          mCells[curCut % mNumCells].activeDecay = 10;
        mLastCut = curCut;
      }

      for (int c = 0; c < mNumCells; c++)
      {
        Cell &cell = mCells[c];

        float n = sampleNoise(cell.noisePhase + mTime * 0.3f, mTime * 0.2f);
        float base = 0.12f + n * 0.12f;

        float active = 0.0f;
        if (cell.activeDecay > 0)
        {
          active = 0.55f * ((float)cell.activeDecay / 10.0f);
          cell.activeDecay--;
        }

        float b = base + active;
        if (b > 1.0f)
          b = 1.0f;
        int gray = (int)(b * 15.0f);
        if (gray < 1)
          gray = 1;
        if (gray > 15)
          gray = 15;

        int cl = left + cell.left;
        int cb = bot + cell.bottom;
        int cr = left + cell.right;
        int ct = bot + cell.top;

        if (cr - cl > 2 && ct - cb > 2)
          fb.fill((od::Color)gray, cl + 1, cb + 1, cr - 1, ct - 1);

        fb.box(GRAY3, cl, cb, cr, ct);
      }
    }

#endif

  private:
    static const int kMaxCells = 32;
    static const int kNoiseLUTSize = 64;

    struct Cell
    {
      int left, bottom, right, top;
      float noisePhase;
      int activeDecay;
    };

    Colmatage *mpColmatage = 0;
    Cell mCells[kMaxCells];
    int mNumCells;
    float mNoiseLUT[kNoiseLUTSize * kNoiseLUTSize];
    float mTime;
    int mLastPhrasePos;
    int mLastCut;
    uint32_t mRng;

    float rngFloat()
    {
      mRng = mRng * 1664525u + 1013904223u;
      return (float)(mRng >> 1) / (float)0x7FFFFFFF;
    }

    void repartition(int w, int h, float blockSizeBias)
    {
      mNumCells = 0;
      mRng = mRng * 1664525u + 1013904223u;
      int minArea = (int)(64.0f + blockSizeBias * 448.0f);
      splitRect(0, 0, w, h, 0, minArea);
    }

    void splitRect(int l, int b, int r, int t, int depth, int minArea)
    {
      int area = (r - l) * (t - b);
      if (area < minArea || depth > 5 || mNumCells >= kMaxCells - 1)
      {
        if (mNumCells < kMaxCells)
        {
          Cell &c = mCells[mNumCells];
          c.left = l;
          c.bottom = b;
          c.right = r;
          c.top = t;
          c.noisePhase = rngFloat() * 100.0f;
          c.activeDecay = 0;
          mNumCells++;
        }
        return;
      }

      bool splitH = (r - l) < (t - b);
      float frac = 0.3f + rngFloat() * 0.4f;

      if (splitH)
      {
        int mid = b + (int)((float)(t - b) * frac);
        if (mid <= b + 2)
          mid = b + 3;
        if (mid >= t - 2)
          mid = t - 3;
        splitRect(l, b, r, mid, depth + 1, minArea);
        splitRect(l, mid, r, t, depth + 1, minArea);
      }
      else
      {
        int mid = l + (int)((float)(r - l) * frac);
        if (mid <= l + 2)
          mid = l + 3;
        if (mid >= r - 2)
          mid = r - 3;
        splitRect(l, b, mid, t, depth + 1, minArea);
        splitRect(mid, b, r, t, depth + 1, minArea);
      }
    }

    static inline float lerp(float a, float b, float t)
    {
      return a + (b - a) * t;
    }

    void bakeNoiseLUT()
    {
      int perm[512];
      for (int i = 0; i < 256; i++)
        perm[i] = i;
      for (int i = 255; i > 0; i--)
      {
        int j = (int)(rngFloat() * (float)(i + 1));
        if (j > i)
          j = i;
        int tmp = perm[i];
        perm[i] = perm[j];
        perm[j] = tmp;
      }
      for (int i = 0; i < 256; i++)
        perm[256 + i] = perm[i];

      float inv = 1.0f / (float)kNoiseLUTSize;
      for (int y = 0; y < kNoiseLUTSize; y++)
      {
        float ny = (float)y * 4.0f * inv;
        for (int x = 0; x < kNoiseLUTSize; x++)
        {
          float nx = (float)x * 4.0f * inv;
          mNoiseLUT[y * kNoiseLUTSize + x] = perlinEval(perm, nx, ny);
        }
      }
    }

    static float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

    static float grad(int hash, float x, float y)
    {
      int h = hash & 3;
      float u = (h < 2) ? x : y;
      float v = (h < 2) ? y : x;
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
      float x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u);
      float x2 = lerp(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u);
      return lerp(x1, x2, v) * 0.5f + 0.5f;
    }

    float sampleNoise(float u, float v) const
    {
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
  };

}
