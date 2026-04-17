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
      memset(mCurrent, 0, sizeof(mCurrent));
      memset(mTarget, 0, sizeof(mTarget));
      mNumCurrentCells = 0;
      mNumTargetCells = 0;
      mTime = 0.0f;
      mLastPhrasePos = -1;
      mLastCut = -1;
      mMorphProgress = 1.0f;
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

      int phrasePos = mpColmatage->getPhrasePosition();
      int phraseLen = mpColmatage->getPhraseLength();
      float phraseProgress = (phraseLen > 0) ? (float)phrasePos / (float)phraseLen : 0.0f;

      if (phrasePos < mLastPhrasePos || mNumCurrentCells == 0)
      {
        float bs = mpColmatage->getBlockSize();
        generateNewTarget(w, h, bs);
      }
      mLastPhrasePos = phrasePos;

      mTime += 0.015f;

      if (mMorphProgress < 1.0f)
      {
        mMorphProgress += 0.04f;
        if (mMorphProgress >= 1.0f)
        {
          mMorphProgress = 1.0f;
          for (int i = 0; i < mNumTargetCells; i++)
            mCurrent[i] = mTarget[i];
          mNumCurrentCells = mNumTargetCells;
        }
      }

      int curCut = mpColmatage->getCurrentCut();
      if (curCut != mLastCut)
      {
        int n = (mMorphProgress < 1.0f) ? mNumTargetCells : mNumCurrentCells;
        if (n > 0)
        {
          int cellIdx = curCut % n;
          if (mMorphProgress < 1.0f)
            mTarget[cellIdx].activeDecay = 12;
          else
            mCurrent[cellIdx].activeDecay = 12;
        }
        mLastCut = curCut;
      }

      int numDraw = (mMorphProgress < 1.0f) ? mNumTargetCells : mNumCurrentCells;
      if (numDraw > mNumCurrentCells)
        numDraw = mNumTargetCells;

      int maxCells = (mNumCurrentCells > mNumTargetCells) ? mNumCurrentCells : mNumTargetCells;

      for (int c = 0; c < maxCells; c++)
      {
        float dl, db, dr, dt;
        float noisePhase;
        int activeDecay;

        if (mMorphProgress >= 1.0f)
        {
          if (c >= mNumCurrentCells) continue;
          CellRect &cur = mCurrent[c];
          dl = (float)cur.left;
          db = (float)cur.bottom;
          dr = (float)cur.right;
          dt = (float)cur.top;
          noisePhase = cur.noisePhase;
          activeDecay = cur.activeDecay;
          if (cur.activeDecay > 0) cur.activeDecay--;
        }
        else
        {
          CellRect *src = (c < mNumCurrentCells) ? &mCurrent[c] : nullptr;
          CellRect *dst = (c < mNumTargetCells) ? &mTarget[c] : nullptr;

          if (src && dst)
          {
            float t = mMorphProgress;
            dl = lerp((float)src->left, (float)dst->left, t);
            db = lerp((float)src->bottom, (float)dst->bottom, t);
            dr = lerp((float)src->right, (float)dst->right, t);
            dt = lerp((float)src->top, (float)dst->top, t);
            noisePhase = lerp(src->noisePhase, dst->noisePhase, t);
            activeDecay = dst->activeDecay;
            if (dst->activeDecay > 0) dst->activeDecay--;
          }
          else if (dst)
          {
            float t = mMorphProgress;
            float cx = ((float)dst->left + (float)dst->right) * 0.5f;
            float cy = ((float)dst->bottom + (float)dst->top) * 0.5f;
            dl = lerp(cx, (float)dst->left, t);
            db = lerp(cy, (float)dst->bottom, t);
            dr = lerp(cx, (float)dst->right, t);
            dt = lerp(cy, (float)dst->top, t);
            noisePhase = dst->noisePhase;
            activeDecay = dst->activeDecay;
            if (dst->activeDecay > 0) dst->activeDecay--;
          }
          else if (src)
          {
            float t = 1.0f - mMorphProgress;
            float cx = ((float)src->left + (float)src->right) * 0.5f;
            float cy = ((float)src->bottom + (float)src->top) * 0.5f;
            dl = lerp(cx, (float)src->left, t);
            db = lerp(cy, (float)src->bottom, t);
            dr = lerp(cx, (float)src->right, t);
            dt = lerp(cy, (float)src->top, t);
            noisePhase = src->noisePhase;
            activeDecay = src->activeDecay;
            if (src->activeDecay > 0) src->activeDecay--;
          }
          else
            continue;
        }

        int cl = left + (int)(dl + 0.5f);
        int cb = bot + (int)(db + 0.5f);
        int cr = left + (int)(dr + 0.5f);
        int ct = bot + (int)(dt + 0.5f);

        if (cr <= cl || ct <= cb) continue;

        float n = sampleNoise(noisePhase + mTime * 0.3f, mTime * 0.2f);

        float cellCx = (dl + dr) * 0.5f / (float)w;
        float cellCy = (db + dt) * 0.5f / (float)h;
        float diag = (cellCx + (1.0f - cellCy)) * 0.5f;
        float phraseSweep = phraseProgress - diag;
        if (phraseSweep < 0.0f) phraseSweep = 0.0f;
        if (phraseSweep > 1.0f) phraseSweep = 1.0f;
        phraseSweep = phraseSweep * phraseSweep;

        float base = phraseSweep * 0.25f + n * 0.08f;

        float active = 0.0f;
        if (activeDecay > 0)
          active = 0.65f * ((float)activeDecay / 12.0f);

        float b = base + active;
        if (b > 1.0f) b = 1.0f;

        int gray = (int)(b * 15.0f);
        if (gray > 15) gray = 15;

        if (cr - cl > 2 && ct - cb > 2)
          fb.fill((od::Color)gray, cl + 1, cb + 1, cr - 1, ct - 1);
        else if (cr - cl > 0 && ct - cb > 0)
          fb.fill((od::Color)gray, cl, cb, cr, ct);

        if (cr - cl > 2 && ct - cb > 2)
          fb.box(GRAY3, cl, cb, cr, ct);
      }
    }

#endif

  private:
    static const int kMaxCells = 32;
    static const int kNoiseLUTSize = 64;

    struct CellRect
    {
      int left, bottom, right, top;
      float noisePhase;
      int activeDecay;
    };

    Colmatage *mpColmatage = 0;
    CellRect mCurrent[kMaxCells];
    CellRect mTarget[kMaxCells];
    int mNumCurrentCells;
    int mNumTargetCells;
    float mNoiseLUT[kNoiseLUTSize * kNoiseLUTSize];
    float mTime;
    int mLastPhrasePos;
    int mLastCut;
    float mMorphProgress;
    uint32_t mRng;

    float rngFloat()
    {
      mRng = mRng * 1664525u + 1013904223u;
      return (float)(mRng >> 1) / (float)0x7FFFFFFF;
    }

    void generateNewTarget(int w, int h, float blockSizeBias)
    {
      if (mMorphProgress >= 1.0f)
      {
        for (int i = 0; i < mNumCurrentCells; i++)
          mCurrent[i] = mCurrent[i];
      }
      else
      {
        for (int i = 0; i < mNumTargetCells; i++)
          mCurrent[i] = mTarget[i];
        mNumCurrentCells = mNumTargetCells;
      }

      mNumTargetCells = 0;
      mRng = mRng * 1664525u + 1013904223u;
      int minArea = (int)(48.0f + blockSizeBias * 464.0f);
      splitRect(mTarget, mNumTargetCells, 0, 0, w, h, 0, minArea);
      mMorphProgress = 0.0f;
    }

    void splitRect(CellRect *cells, int &count, int l, int b, int r, int t, int depth, int minArea)
    {
      int area = (r - l) * (t - b);
      if (area < minArea || depth > 5 || count >= kMaxCells - 1)
      {
        if (count < kMaxCells)
        {
          CellRect &c = cells[count];
          c.left = l;
          c.bottom = b;
          c.right = r;
          c.top = t;
          c.noisePhase = rngFloat() * 100.0f;
          c.activeDecay = 0;
          count++;
        }
        return;
      }

      bool splitH = (r - l) < (t - b);
      float frac = 0.3f + rngFloat() * 0.4f;

      if (splitH)
      {
        int mid = b + (int)((float)(t - b) * frac);
        if (mid <= b + 2) mid = b + 3;
        if (mid >= t - 2) mid = t - 3;
        if (mid <= b || mid >= t) { mid = (b + t) / 2; }
        splitRect(cells, count, l, b, r, mid, depth + 1, minArea);
        splitRect(cells, count, l, mid, r, t, depth + 1, minArea);
      }
      else
      {
        int mid = l + (int)((float)(r - l) * frac);
        if (mid <= l + 2) mid = l + 3;
        if (mid >= r - 2) mid = r - 3;
        if (mid <= l || mid >= r) { mid = (l + r) / 2; }
        splitRect(cells, count, l, b, mid, t, depth + 1, minArea);
        splitRect(cells, count, mid, b, r, t, depth + 1, minArea);
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
        if (j > i) j = i;
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
