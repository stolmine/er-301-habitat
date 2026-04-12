#pragma once

#include <od/graphics/Graphic.h>
#include <Larets.h>
#include <math.h>

namespace stolmine
{
  static const int kContourPts = 32;

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
      for (int i = 0; i < kContourPts; i++)
        mContourR[i] = 0.05f;
    }

    virtual ~LaretOverviewGraphic()
    {
      if (mpLarets)
        mpLarets->release();
    }

#ifndef SWIGLUA

    static inline float catmullRom(float p0, float p1, float p2, float p3,
                                   float t, float tau)
    {
      float t2 = t * t, t3 = t2 * t;
      float a = -tau * p0 + (2.0f - tau) * p1 + (tau - 2.0f) * p2 + tau * p3;
      float b = 2.0f * tau * p0 + (tau - 3.0f) * p1 + (3.0f - 2.0f * tau) * p2 - tau * p3;
      float c = -tau * p0 + tau * p2;
      float d = p1;
      return a * t3 + b * t2 + c * t + d;
    }

    // Evaluate field from small balls only (for contour to merge with step indicators)
    float evalSmallField(float nx, float ny, float *sX, float *sY, float *sStr,
                         int sc, float invR2)
    {
      float field = 0.0f;
      for (int i = 0; i < sc; i++)
      {
        float dx = nx - sX[i], dy = ny - sY[i];
        float d2 = dx * dx + dy * dy;
        float t = 1.0f - d2 * invR2;
        if (t <= 0.0f) continue;
        field += sStr[i] * 0.8f * t * t * t;
      }
      return field;
    }

    void drawContour(od::FrameBuffer &fb, float *radii, float cx, float cy,
                     int w, int h, int left, int bot, int color, float rotOff = 0.0f)
    {
      int prevX = -1, prevY = -1;
      int firstX = -1, firstY = -1;
      int subSteps = 4;
      for (int a = 0; a < kContourPts; a++)
      {
        float r0 = radii[(a - 1 + kContourPts) % kContourPts];
        float r1 = radii[a];
        float r2 = radii[(a + 1) % kContourPts];
        float r3 = radii[(a + 2) % kContourPts];
        for (int s = 0; s < subSteps; s++)
        {
          float t = (float)s / (float)subSteps;
          float r = catmullRom(r0, r1, r2, r3, t, 0.5f);
          if (r < 0.0f) r = 0.0f;
          float angle = ((float)a + (float)s / (float)subSteps) / (float)kContourPts * 6.28318f + rotOff;
          int px = left + (int)((cx + cosf(angle) * r) * (float)(w - 1));
          int py = bot + (int)((cy + sinf(angle) * r) * (float)(h - 1));
          if (px < left) px = left;
          if (px >= left + w) px = left + w - 1;
          if (py < bot) py = bot;
          if (py >= bot + h) py = bot + h - 1;
          if (prevX >= 0)
            fb.line(color, prevX, prevY, px, py);
          else
          { firstX = px; firstY = py; }
          prevX = px; prevY = py;
        }
      }
      if (prevX >= 0 && firstX >= 0)
        fb.line(color, prevX, prevY, firstX, firstY);
    }

    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpLarets)
        return;

      int stepCount = mpLarets->getStepCount();
      int activeStep = mpLarets->getStep();
      if (stepCount < 1)
        return;

      int w = mWidth, h = mHeight;
      int left = mWorldLeft, bot = mWorldBottom;

      // Advance noise LFOs
      for (int i = 0; i < stepCount; i++)
      {
        mNoisePhase[i] += mNoiseFreq[i] / 30.0f;
        if (mNoisePhase[i] > 1.0f) mNoisePhase[i] -= 1.0f;
      }

      // Adaptive layout
      int cols, rows;
      if (stepCount <= 4) { cols = stepCount; rows = 1; }
      else if (stepCount <= 8) { cols = (stepCount + 1) / 2; rows = 2; }
      else { cols = (stepCount + 1) / 2; rows = 2; }

      float stepX[16], stepY[16];
      for (int i = 0; i < stepCount; i++)
      {
        int row = i / cols, col = i % cols;
        int rowCount = (row == 0) ? cols : (stepCount - cols);
        stepX[i] = (rowCount > 1)
          ? 0.15f + (float)col / (float)(rowCount - 1) * 0.7f : 0.5f;
        stepY[i] = (rows > 1)
          ? 0.7f - (float)row * 0.4f : 0.5f;
      }

      // Tempo-adaptive lerp
      float clockSec = mpLarets->getClockPeriodSeconds();
      float lerp = 0.15f;
      if (clockSec < 0.5f) lerp = 0.25f;
      if (clockSec < 0.2f) lerp = 0.4f;
      if (clockSec < 0.1f) lerp = 0.6f;
      if (clockSec < 0.05f) lerp = 0.85f;

      if (mLastActiveStep != activeStep)
      {
        mGhostX = mActiveX; mGhostY = mActiveY;
        for (int i = 0; i < kContourPts; i++)
          mGhostContourR[i] = mContourR[i];
        mTargetX = stepX[activeStep % stepCount];
        mTargetY = stepY[activeStep % stepCount];
        mLastActiveStep = activeStep;
        mStepAge = 0.0f;
      }
      mActiveX += (mTargetX - mActiveX) * lerp;
      mActiveY += (mTargetY - mActiveY) * lerp;
      mGhostX += (mActiveX - mGhostX) * 0.04f;
      mGhostY += (mActiveY - mGhostY) * 0.04f;

      // Radii
      float density = (float)cols / 8.0f;
      if (density < 0.5f) density = 0.5f;
      float smallR = (5.5f / density) / (float)w;
      float bigR = (10.0f / density) / (float)w;
      if (smallR > 8.0f / (float)w) smallR = 8.0f / (float)w;
      if (bigR > 14.0f / (float)w) bigR = 14.0f / (float)w;
      float smallR2 = smallR * smallR;
      float invSmallR2 = 1.0f / smallR2;

      float stepStr[16];
      for (int i = 0; i < stepCount; i++)
        stepStr[i] = 0.7f + 0.3f * sinf(mNoisePhase[i] * 6.28318f);

      // --- Small ball metaball field (per-pixel, cached) ---
      mFrameToggle ^= 1;
      if (mFrameToggle == 0)
      {
        for (int py = 0; py < h; py++)
          for (int px = 0; px < w; px++)
          {
            int gray = mCache[py * 128 + px];
            if (gray > 0) fb.pixel(gray, left + px, bot + py);
          }
      }
      else
      {
        for (int py = 0; py < h; py++)
        {
          float ny = (float)py / (float)(h - 1);
          for (int px = 0; px < w; px++)
          {
            float nx = (float)px / (float)(w - 1);
            float field = evalSmallField(nx, ny, stepX, stepY, stepStr,
                                         stepCount, invSmallR2);
            float sharp = field * field;
            int cellGray = 0;
            if (sharp > 0.03f)
            {
              float blob = (sharp - 0.03f) * 35.0f;
              if (blob > 13.0f) blob = 13.0f;
              cellGray = (int)blob;
            }
            if (px < 128) mCache[py * 128 + px] = (uint8_t)cellGray;
            if (cellGray > 0) fb.pixel(cellGray, left + px, bot + py);
          }
        }
      }

      // --- Contour extraction: radial march from active center ---
      float bigR2 = bigR * bigR;
      float invBigR2 = 1.0f / bigR2;
      float marchStep = bigR * 0.08f;

      mRotAngle += 0.015f;
      if (mRotAngle > 6.28318f) mRotAngle -= 6.28318f;

      for (int a = 0; a < kContourPts; a++)
      {
        float angle = (float)a / (float)kContourPts * 6.28318f + mRotAngle;
        float cosA = cosf(angle), sinA = sinf(angle);
        float r = 0.0f;
        float lastR = 0.0f;
        for (int s = 0; s < 20; s++)
        {
          float px = mActiveX + cosA * r;
          float py = mActiveY + sinA * r;
          // Big ball field
          float d2 = r * r;
          float t = 1.0f - d2 * invBigR2;
          if (t < 0.0f) t = 0.0f;
          float field = 1.2f * t * t * t;
          // Add small ball contributions for merging
          field += evalSmallField(px, py, stepX, stepY, stepStr,
                                  stepCount, invSmallR2);
          if (field * field < 0.03f) break;
          lastR = r;
          r += marchStep;
        }
        mContourR[a] = lastR;
      }

      // --- Per-effect vertex modification ---
      int fxType = mpLarets->getStepType(activeStep);
      float fxParam = mpLarets->getStepParam(activeStep);
      mVizPhase += 0.08f;
      if (mVizPhase > 6.28318f) mVizPhase -= 6.28318f;
      mStepAge += 1.0f / 30.0f;
      float sp = mStepAge / (clockSec > 0.01f ? clockSec : 0.5f);
      if (sp > 1.0f) sp = 1.0f;

      switch (fxType)
      {
      case 0: break; // Off: clean
      case 1: // Stutter: alternating vertex pulse
        for (int a = 0; a < kContourPts; a++)
        {
          float rate = 3.0f + fxParam * 10.0f;
          float phase = mVizPhase * rate + (float)a * 0.5f;
          mContourR[a] *= 0.6f + 0.4f * (0.5f + 0.5f * sinf(phase));
        }
        break;
      case 2: // Reverse: collapse inward over step
        for (int a = 0; a < kContourPts; a++)
          mContourR[a] *= 1.0f - sp * 0.7f;
        break;
      case 3: // Bitcrush: quantize radii
      {
        int levels = 2 + (int)((1.0f - fxParam) * 6.0f);
        float qStep = bigR / (float)levels;
        for (int a = 0; a < kContourPts; a++)
          mContourR[a] = floorf(mContourR[a] / qStep + 0.5f) * qStep;
        break;
      }
      case 4: // Downsample: skip vertices (gaps)
      {
        int skip = 2 + (int)(fxParam * 6.0f);
        for (int a = 0; a < kContourPts; a++)
          if (a % skip != 0) mContourR[a] = 0.0f;
        break;
      }
      case 5: // Filter: LPF inflate, HPF contract + noise
        if (fxParam < 0.5f)
        {
          float scale = 1.0f + (0.5f - fxParam) * 0.6f;
          for (int a = 0; a < kContourPts; a++) mContourR[a] *= scale;
        }
        else
        {
          float scale = 1.0f - (fxParam - 0.5f) * 0.4f;
          for (int a = 0; a < kContourPts; a++)
          {
            uint32_t h = (a * 374761393u + (uint32_t)(mVizPhase * 100.0f)) * 668265263u;
            float n = (float)((int32_t)(h >> 16) & 0xFF) / 255.0f - 0.5f;
            mContourR[a] = mContourR[a] * scale + n * bigR * 0.3f * (fxParam - 0.5f);
          }
        }
        break;
      case 6: // Pitch shift: uniform scale
        for (int a = 0; a < kContourPts; a++)
          mContourR[a] *= 0.5f + fxParam * 1.2f;
        break;
      case 7: // Tape stop: squash Y (top collapses)
      {
        float squash = 1.0f - sp * 0.8f;
        if (squash < 0.1f) squash = 0.1f;
        for (int a = 0; a < kContourPts; a++)
        {
          float angle = (float)a / (float)kContourPts * 6.28318f;
          float yComp = fabsf(sinf(angle));
          mContourR[a] *= 1.0f - yComp * (1.0f - squash);
        }
        break;
      }
      case 8: // Gate: pulse all radii
      {
        float pulse = 0.3f + 0.7f * (0.5f + 0.5f * sinf(mVizPhase * 3.0f));
        for (int a = 0; a < kContourPts; a++) mContourR[a] *= pulse;
        break;
      }
      case 9: // Distortion: jagged noise
        for (int a = 0; a < kContourPts; a++)
        {
          uint32_t h = (a * 2654435761u + (uint32_t)(mVizPhase * 50.0f)) ^ 0x85ebca6bu;
          float n = (float)((int32_t)(h >> 16) & 0xFF) / 255.0f - 0.5f;
          mContourR[a] += n * bigR * fxParam * 0.8f;
        }
        break;
      case 10: // Shuffle: random perturbation per step
        for (int a = 0; a < kContourPts; a++)
        {
          uint32_t h = (a * 374761393u + (uint32_t)(activeStep * 668265263u));
          float n = (float)((int32_t)(h >> 16) & 0xFF) / 255.0f - 0.5f;
          mContourR[a] += n * bigR * 0.3f;
        }
        break;
      case 11: break; // Delay: handled below with ghost contour
      case 12: break; // Comb: handled below with concentric draws
      }

      // --- Draw main contour ---
      drawContour(fb, mContourR, mActiveX, mActiveY, w, h, left, bot, WHITE, mRotAngle);

      // --- Delay: ghost contour at previous position ---
      if (fxType == 11)
      {
        float ghostR[kContourPts];
        float fade = 0.6f + fxParam * 0.3f;
        for (int a = 0; a < kContourPts; a++)
          ghostR[a] = mGhostContourR[a] * fade;
        drawContour(fb, ghostR, mGhostX, mGhostY, w, h, left, bot, GRAY5, mRotAngle);
        // Second ghost, even dimmer
        float ghostR2f[kContourPts];
        for (int a = 0; a < kContourPts; a++)
          ghostR2f[a] = mGhostContourR[a] * fade * 0.6f;
        drawContour(fb, ghostR2f, mGhostX, mGhostY, w, h, left, bot, GRAY3, mRotAngle);
      }

      // --- Comb: concentric contour copies ---
      if (fxType == 12)
      {
        int rings = 2 + (int)(fxParam * 3.0f);
        for (int r = 1; r <= rings; r++)
        {
          float scale = 1.0f + (float)r * 0.25f;
          float ringR[kContourPts];
          for (int a = 0; a < kContourPts; a++)
            ringR[a] = mContourR[a] * scale;
          int gray = GRAY7 - r * 2;
          if (gray < GRAY1) gray = GRAY1;
          drawContour(fb, ringR, mActiveX, mActiveY, w, h, left, bot, gray, mRotAngle);
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
    float mActiveX = 0.5f, mActiveY = 0.5f;
    float mTargetX = 0.5f, mTargetY = 0.5f;
    float mGhostX = 0.5f, mGhostY = 0.5f;
    int mLastActiveStep = -1;
    float mStepAge = 0.0f;
    float mVizPhase = 0.0f;
    float mNoisePhase[16];
    float mNoiseFreq[16];
    int mFrameToggle = 1;
    uint8_t mCache[128 * 64] = {};
    float mRotAngle = 0.0f;
    float mContourR[kContourPts] = {};
    float mGhostContourR[kContourPts] = {};
  };

} // namespace stolmine
