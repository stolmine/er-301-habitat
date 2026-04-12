#pragma once

#include <od/graphics/Graphic.h>
#include <Larets.h>
#include <math.h>

namespace stolmine
{
  class LaretOverviewGraphic : public od::Graphic
  {
  public:
    LaretOverviewGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height)
    {
      for (int i = 0; i < 128; i++)
      {
        mSlewShape[i] = 0.0f;
        mSnapshot[i] = 0.0f;
      }
    }

    virtual ~LaretOverviewGraphic()
    {
      if (mpLarets)
        mpLarets->release();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpLarets)
        return;

      int w = mWidth, h = mHeight;
      int left = mWorldLeft, bot = mWorldBottom;
      int centerY = bot + h / 2;

      // Snapshot ring buffer every 3 frames to reduce SWIG overhead
      mUpdateCounter++;
      if (mUpdateCounter >= 3)
      {
        mUpdateCounter = 0;
        for (int i = 0; i < 128; i++)
          mSnapshot[i] = mpLarets->getOutputSample(i);
      }

      // DC blocker
      float dcSum = 0.0f;
      for (int i = 0; i < 128; i++)
        dcSum += mSnapshot[i];
      float dc = dcSum / 128.0f;
      mDcState += (dc - mDcState) * 0.1f;

      // Slew
      for (int i = 0; i < 128; i++)
      {
        float s = mSnapshot[i] - mDcState;
        mSlewShape[i] += (s - mSlewShape[i]) * 0.15f;
      }

      // Downsample 128 → w screen positions with min/max envelope
      int nPts = w;
      float screenMin[128], screenMax[128], screenAvg[128];
      float ampScale = (float)(h / 2 - 2);
      for (int px = 0; px < nPts && px < 128; px++)
      {
        int s0 = (px * 128) / nPts;
        int s1 = ((px + 1) * 128) / nPts;
        if (s1 <= s0) s1 = s0 + 1;
        if (s1 > 128) s1 = 128;
        float mn = mSlewShape[s0], mx = mSlewShape[s0], sum = 0.0f;
        for (int j = s0; j < s1; j++)
        {
          float v = mSlewShape[j];
          if (v < mn) mn = v;
          if (v > mx) mx = v;
          sum += v;
        }
        screenMin[px] = mn;
        screenMax[px] = mx;
        screenAvg[px] = sum / (float)(s1 - s0);
      }

      // Active effect
      int activeStep = mpLarets->getStep();
      int fxType = mpLarets->getStepType(activeStep);
      float fxParam = mpLarets->getStepParam(activeStep);
      mVizPhase += 0.06f;
      if (mVizPhase > 6.28318f) mVizPhase -= 6.28318f;

      // Zero line
      fb.hline(GRAY1, left, left + w - 1, centerY);

      // Draw fill + particles per column
      for (int px = 0; px < nPts && px < 128; px++)
      {
        int x = left + px;
        float val = screenAvg[px];

        // Min/max envelope for fill
        int pyMin = centerY - (int)(screenMax[px] * ampScale);
        int pyMax = centerY - (int)(screenMin[px] * ampScale);
        if (pyMin < bot) pyMin = bot;
        if (pyMax > bot + h - 1) pyMax = bot + h - 1;
        if (pyMin > pyMax) { int tmp = pyMin; pyMin = pyMax; pyMax = tmp; }

        // Average position for particle dot
        int py = centerY - (int)(val * ampScale);
        if (py < bot) py = bot;
        if (py > bot + h - 1) py = bot + h - 1;

        int yLo = pyMin;
        int yHi = pyMax;

        // Per-effect fill and particle
        switch (fxType)
        {
        case 0: // Off: dim fill, even particles
          fb.vline(GRAY7, x, yLo, yHi);
          fb.pixel(WHITE, x, py);
          break;

        case 1: // Stutter: clustered groups
        {
          int groupSize = 3 + (int)(fxParam * 5.0f);
          int gap = 2;
          bool inGroup = (px % (groupSize + gap)) < groupSize;
          if (inGroup)
          {
            fb.vline(GRAY9, x, yLo, yHi);
            fb.pixel(WHITE, x, py);
          }
          break;
        }

        case 2: // Reverse: brightness decays L→R
        {
          float fade = (float)(nPts - 1 - px) / (float)(nPts - 1);
          int fillGray = 1 + (int)(fade * 4.0f);
          int dotGray = 3 + (int)(fade * 10.0f);
          fb.vline(fillGray, x, yLo, yHi);
          fb.pixel(dotGray, x, py);
          break;
        }

        case 3: // Bitcrush: quantized Y, staircase
        {
          int levels = 2 + (int)((1.0f - fxParam) * 10.0f);
          float qStep = 2.0f / (float)levels;
          float qVal = floorf(val / qStep + 0.5f) * qStep;
          int qy = centerY - (int)(qVal * ampScale);
          if (qy < bot) qy = bot;
          if (qy > bot + h - 1) qy = bot + h - 1;
          int qLo = (qy < centerY) ? qy : centerY;
          int qHi = (qy < centerY) ? centerY : qy;
          fb.hline(GRAY9, x, (px < nPts - 1) ? left + px + 1 : x, qy);
          fb.vline(GRAY7, x, qLo, qHi);
          fb.pixel(WHITE, x, qy);
          break;
        }

        case 4: // Downsample: sample-and-hold staircase
        {
          int skip = 2 + (int)(fxParam * 4.0f);
          if (px % skip == 0)
            mHeldY = py;
          int hLo = (mHeldY < centerY) ? mHeldY : centerY;
          int hHi = (mHeldY < centerY) ? centerY : mHeldY;
          fb.vline(GRAY7, x, hLo, hHi);
          if (px % skip == 0)
            fb.pixel(WHITE, x, mHeldY);
          break;
        }

        case 5: // Filter: solid fill (LPF) or outline only (HPF)
          if (fxParam < 0.5f)
          {
            fb.vline(GRAY9, x, yLo, yHi);
            fb.pixel(WHITE, x, py);
          }
          else
          {
            fb.pixel(WHITE, x, py);
          }
          break;

        case 6: // Pitch shift: fill height scaled
        {
          float scale = 0.3f + fxParam * 1.4f;
          int scaledY = centerY - (int)(val * ampScale * scale);
          if (scaledY < bot) scaledY = bot;
          if (scaledY > bot + h - 1) scaledY = bot + h - 1;
          int sLo = (scaledY < centerY) ? scaledY : centerY;
          int sHi = (scaledY < centerY) ? centerY : scaledY;
          fb.vline(GRAY7, x, sLo, sHi);
          fb.pixel(WHITE, x, scaledY);
          break;
        }

        case 7: // Tape stop: compress rightward over step age
        {
          float compress = 1.0f;
          float sp = mVizPhase / 6.28318f;
          int cutoff = (int)((float)nPts * (1.0f - sp * 0.8f));
          if (px < cutoff)
          {
            fb.vline(GRAY7, x, yLo, yHi);
            fb.pixel(WHITE, x, py);
          }
          break;
        }

        case 8: // Gate: pulsing brightness
        {
          float pulse = 0.5f + 0.5f * sinf(mVizPhase * 3.0f);
          int fillGray = (int)(pulse * 5.0f);
          int dotGray = 3 + (int)(pulse * 10.0f);
          if (fillGray < 1) fillGray = 1;
          fb.vline(fillGray, x, yLo, yHi);
          fb.pixel(dotGray, x, py);
          break;
        }

        case 9: // Distortion: jittered Y fill
        {
          uint32_t hash = (px * 374761393u + (uint32_t)(mVizPhase * 50.0f)) ^ 0x85ebca6bu;
          float jitter = (float)((int32_t)(hash >> 16) & 0xFF) / 255.0f - 0.5f;
          int jitY = py + (int)(jitter * fxParam * ampScale * 0.4f);
          if (jitY < bot) jitY = bot;
          if (jitY > bot + h - 1) jitY = bot + h - 1;
          int jLo = (jitY < centerY) ? jitY : centerY;
          int jHi = (jitY < centerY) ? centerY : jitY;
          fb.vline(GRAY7, x, jLo, jHi);
          fb.pixel(WHITE, x, jitY);
          break;
        }

        case 10: // Shuffle: segment swaps
        {
          int segs = 4;
          int segLen = nPts / segs;
          if (segLen < 1) segLen = 1;
          int seg = px / segLen;
          uint32_t hash = (seg * 2654435761u) ^ 0x85ebca6bu;
          int swapped = (int)((hash >> 16) % (uint32_t)segs);
          int srcPx = swapped * segLen + (px % segLen);
          if (srcPx >= 0 && srcPx < nPts)
          {
            float sVal = screenAvg[srcPx];
            int sPy = centerY - (int)(sVal * ampScale);
            if (sPy < bot) sPy = bot;
            if (sPy > bot + h - 1) sPy = bot + h - 1;
            int sLo = (sPy < centerY) ? sPy : centerY;
            int sHi = (sPy < centerY) ? centerY : sPy;
            fb.vline(GRAY7, x, sLo, sHi);
            fb.pixel(WHITE, x, sPy);
          }
          break;
        }

        case 11: // Delay: layered chains
        {
          int offset1 = nPts / 4;
          int offset2 = nPts / 2;
          int src1 = (px + offset1) % nPts;
          int src2 = (px + offset2) % nPts;
          int dy1 = centerY - (int)(screenAvg[src1] * ampScale);
          int dy2 = centerY - (int)(screenAvg[src2] * ampScale);
          if (dy2 >= bot && dy2 < bot + h) fb.pixel(GRAY5, x, dy2);
          if (dy1 >= bot && dy1 < bot + h) fb.pixel(GRAY7, x, dy1);
          fb.vline(GRAY7, x, yLo, yHi);
          fb.pixel(WHITE, x, py);
          break;
        }

        case 12: // Comb: rippled fill
        {
          float wobble = sinf((float)px * 0.3f + mVizPhase * 2.0f) * fxParam * ampScale * 0.3f;
          int wobY = py + (int)wobble;
          if (wobY < bot) wobY = bot;
          if (wobY > bot + h - 1) wobY = bot + h - 1;
          int wLo = (wobY < centerY) ? wobY : centerY;
          int wHi = (wobY < centerY) ? centerY : wobY;
          fb.vline(GRAY7, x, wLo, wHi);
          fb.pixel(WHITE, x, wobY);
          break;
        }

        default:
          fb.vline(GRAY7, x, yLo, yHi);
          fb.pixel(WHITE, x, py);
          break;
        }
      }
    }
#endif

    void follow(Larets *p)
    {
      if (mpLarets) mpLarets->release();
      mpLarets = p;
      if (mpLarets) mpLarets->attach();
    }

    void setSelectedStep(int step) { mSelectedStep = step; }
    int getSelectedStep() { return mSelectedStep; }

  private:
    Larets *mpLarets = 0;
    int mSelectedStep = 0;
    float mSlewShape[128];
    float mSnapshot[128];
    int mUpdateCounter = 0;
    float mDcState = 0.0f;
    float mVizPhase = 0.0f;
    int mHeldY = 0;
  };

} // namespace stolmine
