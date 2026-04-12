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
      for (int i = 0; i < 16; i++)
      {
        mNoisePhase[i] = (float)i * 0.39f;
        mNoiseFreq[i] = 0.02f + (float)i * 0.008f;
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

      int stepCount = mpLarets->getStepCount();
      int activeStep = mpLarets->getStep();
      if (stepCount < 1)
        return;

      int w = mWidth;
      int h = mHeight;
      int left = mWorldLeft;
      int bot = mWorldBottom;

      // Advance noise LFOs
      for (int i = 0; i < stepCount; i++)
      {
        mNoisePhase[i] += mNoiseFreq[i] / 30.0f;
        if (mNoisePhase[i] > 1.0f) mNoisePhase[i] -= 1.0f;
      }

      // Frame caching: expensive eval every other frame
      mFrameToggle ^= 1;
      if (mFrameToggle == 0)
      {
        for (int py = 0; py < h; py++)
          for (int px = 0; px < w; px++)
          {
            int gray = mCache[py * 128 + px];
            if (gray > 0)
              fb.pixel(gray, left + px, bot + py);
          }
        return;
      }

      // Adaptive layout: 1-4 = one row, 5-8 = two rows of 4, 9-16 = two rows of 8
      int cols, rows;
      if (stepCount <= 4) { cols = stepCount; rows = 1; }
      else if (stepCount <= 8) { cols = (stepCount + 1) / 2; rows = 2; }
      else { cols = (stepCount + 1) / 2; rows = 2; }

      float stepX[16], stepY[16];
      for (int i = 0; i < stepCount; i++)
      {
        int row = i / cols;
        int col = i % cols;
        int rowCount = (row == 0) ? cols : (stepCount - cols);
        stepX[i] = (rowCount > 1)
          ? 0.15f + (float)col / (float)(rowCount - 1) * 0.7f
          : 0.5f;
        stepY[i] = (rows > 1)
          ? 0.7f - (float)row * 0.4f
          : 0.5f;
      }

      // Adaptive lerp: faster at high tempos so ball keeps up
      float clockSec = mpLarets->getClockPeriodSeconds();
      float lerp = 0.15f;
      if (clockSec < 0.5f) lerp = 0.25f;
      if (clockSec < 0.2f) lerp = 0.4f;
      if (clockSec < 0.1f) lerp = 0.6f;
      if (clockSec < 0.05f) lerp = 0.85f;

      // Update active ball position (2D lerp)
      if (mLastActiveStep != activeStep)
      {
        mTargetX = stepX[activeStep % stepCount];
        mTargetY = stepY[activeStep % stepCount];
        mLastActiveStep = activeStep;
      }
      mActiveX += (mTargetX - mActiveX) * lerp;
      mActiveY += (mTargetY - mActiveY) * lerp;

      // Scale radii based on density
      float density = (float)cols / 8.0f;
      if (density < 0.5f) density = 0.5f;
      float smallR = (5.5f / density) / (float)w;
      float bigR = (10.0f / density) / (float)w;
      if (smallR > 8.0f / (float)w) smallR = 8.0f / (float)w;
      if (bigR > 14.0f / (float)w) bigR = 14.0f / (float)w;
      float smallR2 = smallR * smallR;
      float bigR2 = bigR * bigR;
      float invSmallR2 = 1.0f / smallR2;
      float invBigR2 = 1.0f / bigR2;

      // Noise-modulated strength per step
      float stepStr[16];
      for (int i = 0; i < stepCount; i++)
      {
        float lfo = sinf(mNoisePhase[i] * 6.28318f);
        stepStr[i] = 0.7f + 0.3f * lfo;
      }

      // Per-pixel field evaluation
      for (int py = 0; py < h; py++)
      {
        float ny = (float)py / (float)(h - 1);
        for (int px = 0; px < w; px++)
        {
          float nx = (float)px / (float)(w - 1);
          float field = 0.0f;

          // Small balls
          for (int i = 0; i < stepCount; i++)
          {
            float dx = nx - stepX[i];
            float dy = ny - stepY[i];
            float d2 = dx * dx + dy * dy;
            if (d2 >= smallR2 * 2.0f) continue;
            float t = 1.0f - d2 * invSmallR2;
            if (t < 0.0f) t = 0.0f;
            field += stepStr[i] * 0.8f * t * t * t;
          }

          // Large active ball
          {
            float dx = nx - mActiveX;
            float dy = ny - mActiveY;
            float d2 = dx * dx + dy * dy;
            if (d2 < bigR2 * 2.0f)
            {
              float t = 1.0f - d2 * invBigR2;
              if (t < 0.0f) t = 0.0f;
              field += 1.2f * t * t * t;
            }
          }

          // Threshold and shade
          float sharp = field * field;
          int cellGray = 0;
          if (sharp > 0.03f)
          {
            float blob = (sharp - 0.03f) * 35.0f;
            if (blob > 13.0f) blob = 13.0f;
            cellGray = (int)blob;
          }

          if (px < 128)
            mCache[py * 128 + px] = (uint8_t)cellGray;
          if (cellGray > 0)
            fb.pixel(cellGray, left + px, bot + py);
        }
      }
    }
#endif

    void follow(Larets *p)
    {
      if (mpLarets)
        mpLarets->release();
      mpLarets = p;
      if (mpLarets)
        mpLarets->attach();
    }

    void setSelectedStep(int step) { mSelectedStep = step; }
    int getSelectedStep() { return mSelectedStep; }

  private:
    Larets *mpLarets = 0;
    int mSelectedStep = 0;

    float mActiveX = 0.5f;
    float mActiveY = 0.5f;
    float mTargetX = 0.5f;
    float mTargetY = 0.5f;
    int mLastActiveStep = -1;
    float mNoisePhase[16];
    float mNoiseFreq[16];
    int mFrameToggle = 1;
    uint8_t mCache[128 * 64] = {};
  };

} // namespace stolmine
