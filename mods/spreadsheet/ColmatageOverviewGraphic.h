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
      memset(mTree, 0, sizeof(mTree));
      mTime = 0.0f;
      mLastPhrasePos = -1;
      mLastCut = -1;
      mNumLeaves = 0;
      mRng = 31415;
      bakeNoiseLUT();
      initTree(0, 0, 0, width, height, 0);
      retarget(0.5f);
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

      int left = mWorldLeft;
      int bot = mWorldBottom;

      int phrasePos = mpColmatage->getPhrasePosition();
      int phraseLen = mpColmatage->getPhraseLength();
      float phraseProgress = (phraseLen > 0) ? (float)phrasePos / (float)phraseLen : 0.0f;

      if (phrasePos < mLastPhrasePos)
      {
        float bs = mpColmatage->getBlockSize();
        retarget(bs);
      }
      mLastPhrasePos = phrasePos;

      mTime += 0.015f;

      for (int i = 0; i < kMaxNodes; i++)
      {
        BSPNode &n = mTree[i];
        n.splitProgress += (n.splitTarget - n.splitProgress) * 0.045f;
        if (n.splitProgress < 0.005f) n.splitProgress = 0.0f;
        if (n.splitProgress > 0.995f) n.splitProgress = 1.0f;
      }

      collectLeaves();

      int curCut = mpColmatage->getCurrentCut();
      if (curCut != mLastCut && mNumLeaves > 0)
      {
        int leafIdx = curCut % mNumLeaves;
        mTree[mLeafIndices[leafIdx]].activeDecay = 12;
        mLastCut = curCut;
      }

      drawNode(fb, 0, left, bot, left + mWidth, bot + mHeight, phraseProgress);
    }

#endif

  private:
    static const int kTreeDepth = 5;
    static const int kMaxNodes = 63;
    static const int kMaxLeaves = 32;
    static const int kNoiseLUTSize = 64;

    struct BSPNode
    {
      bool splitH;
      float splitFrac;
      float splitProgress;
      float splitTarget;
      float targetFrac;
      float noisePhase;
      int activeDecay;
    };

    Colmatage *mpColmatage = 0;
    BSPNode mTree[kMaxNodes];
    int mLeafIndices[kMaxLeaves];
    int mNumLeaves;
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

    static inline int childA(int i) { return 2 * i + 1; }
    static inline int childB(int i) { return 2 * i + 2; }
    static inline bool hasChildren(int i) { return childA(i) < kMaxNodes; }

    void initTree(int idx, int l, int b, int r, int t, int depth)
    {
      if (idx >= kMaxNodes) return;
      BSPNode &n = mTree[idx];
      n.splitH = (r - l) < (t - b);
      n.splitFrac = 0.3f + rngFloat() * 0.4f;
      n.targetFrac = n.splitFrac;
      n.splitProgress = 0.0f;
      n.splitTarget = 0.0f;
      n.noisePhase = rngFloat() * 100.0f;
      n.activeDecay = 0;

      if (depth < kTreeDepth && hasChildren(idx))
      {
        int midH, midV;
        if (n.splitH)
        {
          midH = b + (int)((float)(t - b) * n.splitFrac);
          initTree(childA(idx), l, b, r, midH, depth + 1);
          initTree(childB(idx), l, midH, r, t, depth + 1);
        }
        else
        {
          midV = l + (int)((float)(r - l) * n.splitFrac);
          initTree(childA(idx), l, b, midV, t, depth + 1);
          initTree(childB(idx), midV, b, r, t, depth + 1);
        }
      }
    }

    void retarget(float blockSizeBias)
    {
      mRng = mRng * 1664525u + 1013904223u;
      int minArea = (int)(48.0f + blockSizeBias * 464.0f);
      retargetNode(0, 0, 0, mWidth, mHeight, 0, minArea);
    }

    void retargetNode(int idx, int l, int b, int r, int t, int depth, int minArea)
    {
      if (idx >= kMaxNodes) return;
      BSPNode &n = mTree[idx];
      int area = (r - l) * (t - b);

      bool canSplit = (depth < kTreeDepth) && hasChildren(idx) &&
                      (area >= minArea) && (r - l > 4) && (t - b > 4);

      if (canSplit)
      {
        n.splitTarget = 1.0f;
        n.targetFrac = 0.3f + rngFloat() * 0.4f;
        n.splitH = (r - l) < (t - b);

        if (n.splitH)
        {
          int mid = b + (int)((float)(t - b) * n.targetFrac);
          retargetNode(childA(idx), l, b, r, mid, depth + 1, minArea);
          retargetNode(childB(idx), l, mid, r, t, depth + 1, minArea);
        }
        else
        {
          int mid = l + (int)((float)(r - l) * n.targetFrac);
          retargetNode(childA(idx), l, b, mid, t, depth + 1, minArea);
          retargetNode(childB(idx), mid, b, r, t, depth + 1, minArea);
        }
      }
      else
      {
        n.splitTarget = 0.0f;
        collapseChildren(idx);
      }

      // no instant snap — let per-frame slew in drawNode handle it
    }

    void collapseChildren(int idx)
    {
      if (idx >= kMaxNodes || !hasChildren(idx)) return;
      mTree[childA(idx)].splitTarget = 0.0f;
      mTree[childB(idx)].splitTarget = 0.0f;
      collapseChildren(childA(idx));
      collapseChildren(childB(idx));
    }

    void collectLeaves()
    {
      mNumLeaves = 0;
      collectLeavesR(0);
    }

    void collectLeavesR(int idx)
    {
      if (idx >= kMaxNodes) return;
      BSPNode &n = mTree[idx];
      if (n.splitProgress > 0.5f && hasChildren(idx))
      {
        collectLeavesR(childA(idx));
        collectLeavesR(childB(idx));
      }
      else
      {
        if (mNumLeaves < kMaxLeaves)
          mLeafIndices[mNumLeaves++] = idx;
      }
    }

    void drawNode(od::FrameBuffer &fb, int idx, int l, int b, int r, int t, float phraseProgress)
    {
      if (idx >= kMaxNodes || r <= l || t <= b) return;

      BSPNode &n = mTree[idx];

      n.splitFrac += (n.targetFrac - n.splitFrac) * 0.045f;

      if (n.splitProgress > 0.01f && hasChildren(idx))
      {
        float prog = n.splitProgress;
        int midFull;
        if (n.splitH)
          midFull = b + (int)((float)(t - b) * n.splitFrac);
        else
          midFull = l + (int)((float)(r - l) * n.splitFrac);

        int center;
        if (n.splitH)
          center = (b + t) / 2;
        else
          center = (l + r) / 2;

        int mid = center + (int)((float)(midFull - center) * prog);

        if (n.splitH)
        {
          if (mid <= b) mid = b + 1;
          if (mid >= t) mid = t - 1;
          drawNode(fb, childA(idx), l, b, r, mid, phraseProgress);
          drawNode(fb, childB(idx), l, mid, r, t, phraseProgress);

          int edgeGray = (int)(3.0f * prog);
          if (edgeGray > 0)
            fb.hline((od::Color)edgeGray, l, r - 1, mid);
        }
        else
        {
          if (mid <= l) mid = l + 1;
          if (mid >= r) mid = r - 1;
          drawNode(fb, childA(idx), l, b, mid, t, phraseProgress);
          drawNode(fb, childB(idx), mid, b, r, t, phraseProgress);

          int edgeGray = (int)(3.0f * prog);
          if (edgeGray > 0)
            fb.vline((od::Color)edgeGray, mid, b, t - 1);
        }
      }
      else
      {
        float cx = ((float)(l + r) * 0.5f) / (float)mWidth;
        float cy = ((float)(b + t) * 0.5f - (float)mWorldBottom) / (float)mHeight;
        float diag = (cx + (1.0f - cy)) * 0.5f;
        float sweep = phraseProgress * 1.4f - diag * 0.8f;
        if (sweep < 0.0f) sweep = 0.0f;
        if (sweep > 1.0f) sweep = 1.0f;
        sweep = sweep * sweep;

        float noise = sampleNoise(n.noisePhase + mTime * 0.3f, mTime * 0.2f);
        float base = sweep * 0.25f + noise * 0.08f;

        float active = 0.0f;
        if (n.activeDecay > 0)
        {
          active = 0.65f * ((float)n.activeDecay / 12.0f);
          n.activeDecay--;
        }

        float brightness = base + active;
        if (brightness > 1.0f) brightness = 1.0f;

        int gray = (int)(brightness * 15.0f);
        if (gray > 15) gray = 15;

        if (r - l > 2 && t - b > 2)
        {
          fb.fill((od::Color)gray, l + 1, b + 1, r - 1, t - 1);
          fb.box(GRAY3, l, b, r, t);
        }
        else
        {
          fb.fill((od::Color)gray, l, b, r, t);
        }
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
