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
      mSweepPhase = 0.0f;
      mLastPhrasePos = -1;
      mLastCut = -1;
      mNumLeaves = 0;
      mLastActivatedLeaf = -1;
      memset(mVisited, 0, sizeof(mVisited));
      mVisitCount = 0;
      mRng = 31415;
      bakeNoiseLUT();
      initTree(0, 0, 0, width, height, 0);

      mActualGridW = width / kTileSize;
      mActualGridH = height / kTileSize;
      if (mActualGridW > kGridW) mActualGridW = kGridW;
      if (mActualGridH > kGridH) mActualGridH = kGridH;

      for (int i = 0; i < kGridW * kGridH; i++)
      {
        mGrid[i].brightness = 0.0f;
        mGrid[i].target = 0.0f;
        mGrid[i].slewRate = 0.06f + sampleNoise(
          (float)(i % kGridW) / (float)kGridW * 3.0f,
          (float)(i / kGridW) / (float)kGridH * 3.0f) * 0.06f;
      }

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
      int w = mWidth;
      int h = mHeight;
      if (w > SECTION_PLY) w = SECTION_PLY;
      if (h > 64) h = 64;

      fb.clear(left, bot, left + w, bot + h);

      int gw = w / kTileSize;
      int gh = h / kTileSize;
      if (gw > kGridW) gw = kGridW;
      if (gh > kGridH) gh = kGridH;
      int rightClip = left + w;
      int topClip = bot + h;

      int phrasePos = mpColmatage->getPhrasePosition();
      int phraseLen = mpColmatage->getPhraseLength();
      float phraseProgress = (phraseLen > 0) ? (float)phrasePos / (float)phraseLen : 0.0f;

      float phraseSec = (phraseLen > 0) ? (float)phraseLen * 0.5f : 4.0f;
      float period = 2.0f + phraseSec * 0.5f;
      mSweepPhase += 1.0f / (period * 60.0f);

      if (phrasePos < mLastPhrasePos)
      {
        float bs = mpColmatage->getBlockSize();
        retarget(bs);
        memset(mVisited, 0, sizeof(mVisited));
        mVisitCount = 0;
        mLastActivatedLeaf = -1;
      }
      mLastPhrasePos = phrasePos;

      mTime += 0.015f;

      for (int i = 0; i < kMaxNodes; i++)
      {
        BSPNode &n = mTree[i];
        n.splitProgress += (n.splitTarget - n.splitProgress) * 0.045f;
        if (n.splitProgress < 0.005f) n.splitProgress = 0.0f;
        if (n.splitProgress > 0.995f) n.splitProgress = 1.0f;
        n.splitFrac += (n.targetFrac - n.splitFrac) * 0.045f;
      }

      collectLeaves();

      int curCut = mpColmatage->getCurrentCut();
      if (curCut != mLastCut && mNumLeaves > 0)
      {
        int picked = pickNextLeaf();
        if (picked >= 0)
        {
          mTree[mLeafIndices[picked]].activeDecay = 12;
          mVisited[picked] = true;
          mVisitCount++;
          mLastActivatedLeaf = picked;
        }
        mLastCut = curCut;
      }

      for (int i = 0; i < kMaxNodes; i++)
      {
        if (mTree[i].activeDecay > 0)
          mTree[i].activeDecay--;
      }

      float invW = 1.0f / (float)w;
      float invH = 1.0f / (float)h;

      for (int ty = 0; ty < gh; ty++)
      {
        for (int tx = 0; tx < gw; tx++)
        {
          int px = tx * kTileSize + kTileSize / 2;
          int py = ty * kTileSize + kTileSize / 2;

          int leafIdx = walkTree(px, py);
          BSPNode &leaf = mTree[leafIdx];

          float cx = (float)px * invW;
          float cy = (float)py * invH;

          float angle = mSweepPhase * 6.2832f;
          float ca = cosf(angle);
          float sa = sinf(angle);
          float scale = 1.5f + 0.5f * sinf(mSweepPhase * 3.7f);

          float ru = (cx * ca - cy * sa) * scale + mSweepPhase * 0.4f;
          float rv = (cx * sa + cy * ca) * scale + mSweepPhase * 0.3f;

          float fieldRaw = sampleNoise(ru, rv);
          float field = (fieldRaw - 0.35f) * (1.0f / 0.65f);
          if (field < 0.0f) field = 0.0f;

          float breath = sampleNoise(leaf.noisePhase + mTime * 0.25f,
                                     mTime * 0.15f);
          breath = (breath - 0.3f) * (1.0f / 0.7f);
          if (breath < 0.0f) breath = 0.0f;

          float phraseSine = 0.5f - 0.5f * cosf(phraseProgress * 6.2832f);

          float base = field * 0.10f + breath * 0.04f
                     + phraseSine * 0.06f;

          float active = 0.0f;
          if (leaf.activeDecay > 0)
            active = 0.7f * ((float)leaf.activeDecay / 12.0f);

          Tile &tile = mGrid[ty * kGridW + tx];
          tile.target = base + active;
          if (tile.target > 1.0f) tile.target = 1.0f;

          tile.brightness += (tile.target - tile.brightness) * tile.slewRate;

          int gray = (int)(tile.brightness * 15.0f);
          if (gray < 0) gray = 0;
          if (gray > 15) gray = 15;

          int dl = left + tx * kTileSize;
          int db = bot + ty * kTileSize;
          int dr = dl + kTileSize;
          int dt = db + kTileSize;
          if (dr > rightClip) dr = rightClip;
          if (dt > topClip) dt = topClip;
          if (dl < dr && db < dt)
            fb.fill((od::Color)gray, dl, db, dr, dt);
        }
      }
    }

#endif

  private:
    static const int kTileSize = 2;
    static const int kGridW = 21;
    static const int kGridH = 32;
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

    struct Tile
    {
      float brightness;
      float target;
      float slewRate;
    };

    Colmatage *mpColmatage = 0;
    int mActualGridW;
    int mActualGridH;
    BSPNode mTree[kMaxNodes];
    int mLeafIndices[kMaxLeaves];
    int mNumLeaves;
    Tile mGrid[kGridW * kGridH];
    float mNoiseLUT[kNoiseLUTSize * kNoiseLUTSize];
    float mTime;
    float mSweepPhase;
    int mLastPhrasePos;
    int mLastCut;
    int mLastActivatedLeaf;
    bool mVisited[kMaxLeaves];
    int mVisitCount;
    uint32_t mRng;

    float rngFloat()
    {
      mRng = mRng * 1664525u + 1013904223u;
      return (float)(mRng >> 1) / (float)0x7FFFFFFF;
    }

    static inline int childA(int i) { return 2 * i + 1; }
    static inline int childB(int i) { return 2 * i + 2; }
    static inline bool hasChildren(int i) { return childA(i) < kMaxNodes; }

    int walkTree(int px, int py)
    {
      int idx = 0;
      int l = 0, b = 0, r = mWidth, t = mHeight;
      while (hasChildren(idx) && mTree[idx].splitProgress > 0.5f)
      {
        BSPNode &n = mTree[idx];
        if (n.splitH)
        {
          int mid = b + (int)((float)(t - b) * n.splitFrac);
          if (py < mid) { t = mid; idx = childA(idx); }
          else          { b = mid; idx = childB(idx); }
        }
        else
        {
          int mid = l + (int)((float)(r - l) * n.splitFrac);
          if (px < mid) { r = mid; idx = childA(idx); }
          else          { l = mid; idx = childB(idx); }
        }
      }
      return idx;
    }

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
        if (n.splitH)
        {
          int mid = b + (int)((float)(t - b) * n.splitFrac);
          initTree(childA(idx), l, b, r, mid, depth + 1);
          initTree(childB(idx), l, mid, r, t, depth + 1);
        }
        else
        {
          int mid = l + (int)((float)(r - l) * n.splitFrac);
          initTree(childA(idx), l, b, mid, t, depth + 1);
          initTree(childB(idx), mid, b, r, t, depth + 1);
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
    }

    void collapseChildren(int idx)
    {
      if (idx >= kMaxNodes || !hasChildren(idx)) return;
      mTree[childA(idx)].splitTarget = 0.0f;
      mTree[childB(idx)].splitTarget = 0.0f;
      collapseChildren(childA(idx));
      collapseChildren(childB(idx));
    }

    int pickNextLeaf()
    {
      if (mNumLeaves == 0) return -1;

      if (mVisitCount >= mNumLeaves)
      {
        memset(mVisited, 0, sizeof(mVisited));
        mVisitCount = 0;
      }

      if (mLastActivatedLeaf < 0 || mLastActivatedLeaf >= mNumLeaves)
      {
        int start = (int)(rngFloat() * (float)mNumLeaves);
        if (start >= mNumLeaves) start = mNumLeaves - 1;
        return start;
      }

      BSPNode &last = mTree[mLeafIndices[mLastActivatedLeaf]];
      float lastCx, lastCy;
      leafCenter(mLastActivatedLeaf, lastCx, lastCy);

      int bestIdx = -1;
      float bestDist = 1e10f;
      for (int i = 0; i < mNumLeaves; i++)
      {
        if (mVisited[i]) continue;
        float cx, cy;
        leafCenter(i, cx, cy);
        float dx = cx - lastCx;
        float dy = cy - lastCy;
        float dist = dx * dx + dy * dy + rngFloat() * 200.0f;
        if (dist < bestDist) { bestDist = dist; bestIdx = i; }
      }

      return (bestIdx >= 0) ? bestIdx : (int)(rngFloat() * (float)mNumLeaves);
    }

    void leafCenter(int leafArrayIdx, float &cx, float &cy)
    {
      int nodeIdx = mLeafIndices[leafArrayIdx];
      int l = 0, b = 0, r = mWidth, t = mHeight;
      int idx = 0;
      // walk to the node to find its bounds
      while (idx != nodeIdx && hasChildren(idx) && mTree[idx].splitProgress > 0.5f)
      {
        BSPNode &n = mTree[idx];
        if (n.splitH)
        {
          int mid = b + (int)((float)(t - b) * n.splitFrac);
          if (nodeIdx <= childA(idx) + countDescendants(childA(idx)))
            { t = mid; idx = childA(idx); }
          else
            { b = mid; idx = childB(idx); }
        }
        else
        {
          int mid = l + (int)((float)(r - l) * n.splitFrac);
          if (nodeIdx <= childA(idx) + countDescendants(childA(idx)))
            { r = mid; idx = childA(idx); }
          else
            { l = mid; idx = childB(idx); }
        }
      }
      cx = (float)(l + r) * 0.5f;
      cy = (float)(b + t) * 0.5f;
    }

    static int countDescendants(int idx)
    {
      if (idx >= kMaxNodes) return 0;
      return 1 + countDescendants(childA(idx)) + countDescendants(childB(idx));
    }

    void collectLeaves()
    {
      mNumLeaves = 0;
      collectLeavesR(0);
    }

    void collectLeavesR(int idx)
    {
      if (idx >= kMaxNodes) return;
      if (mTree[idx].splitProgress > 0.5f && hasChildren(idx))
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
