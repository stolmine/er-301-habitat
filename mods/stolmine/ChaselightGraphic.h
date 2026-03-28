#pragma once

#include <od/graphics/Graphic.h>
#include <od/objects/Parameter.h>
#include <GateSeq.h>
#include <math.h>
#include <stdio.h>

namespace stolmine
{
  class ChaselightGraphic : public od::Graphic
  {
  public:
    ChaselightGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~ChaselightGraphic()
    {
      if (mpSeq)
        mpSeq->release();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpSeq)
        return;

      int seqLen = mpSeq->getSeqLength();
      int playhead = mpSeq->getStep();

      // Page based on selected step
      int page = mSelectedStep / 16;
      int pageStart = page * 16;

      // Velocity bar graph top row (selected step)
      int totalPages = (seqLen + 15) / 16;
      float vel = mpSeq->getStepVelocity(mSelectedStep);
      int barLeft = mWorldLeft + 2;
      int barRight = mWorldLeft + mWidth - 3;
      int barH = 4;
      int barBottom = mWorldBottom + mHeight - 3 - barH / 2;
      int barTop = barBottom + barH;

      fb.box(GRAY3, barLeft, barBottom, barRight, barTop);
      int fillWidth = (int)((barRight - barLeft - 1) * vel);
      if (fillWidth > 0)
        fb.fill(WHITE, barLeft + 1, barBottom + 1, barLeft + fillWidth, barTop - 1);

      // 4x4 grid layout
      const int cols = 4;
      const int rows = 4;
      const int cellW = mWidth / cols;
      const int cellH = (mHeight - 14) / rows; // reserve 14px for progress bar + indicator
      const int r = 3; // circle radius

      for (int i = 0; i < 16; i++)
      {
        int step = pageStart + i;
        if (step >= seqLen)
          break;

        int col = i % cols;
        int row = i / cols;
        int cx = mWorldLeft + col * cellW + cellW / 2;
        int cy = mWorldBottom + mHeight - 8 - row * cellH - cellH / 2;

        bool isOn = mpSeq->getStepGate(step);
        bool isPlayhead = (step == playhead);
        bool isCursor = (step == mSelectedStep);

        // Draw step circle
        if (isOn)
        {
          if (isPlayhead)
            fb.fillCircle(WHITE, cx, cy, r + 1); // bright, slightly larger
          else
            fb.fillCircle(WHITE, cx, cy, r);
        }
        else
        {
          if (isPlayhead)
            fb.circle(GRAY10, cx, cy, r + 1);
          else
            fb.circle(GRAY5, cx, cy, r);
        }

        // Blinking cursor outline
        if (isCursor && sTween > 0.0f)
        {
          fb.circle(WHITE, cx, cy, r + 2);
        }
      }

      // Page indicator dots at bottom
      int dotSpacing = mWidth / (totalPages + 1);
      for (int p = 0; p < totalPages; p++)
      {
        int dx = mWorldLeft + dotSpacing * (p + 1);
        int dy = mWorldBottom + 3;
        if (p == page)
          fb.fillCircle(WHITE, dx, dy, 2);
        else
          fb.circle(GRAY5, dx, dy, 1);
      }
    }
#endif

    void follow(GateSeq *pSeq)
    {
      if (mpSeq)
        mpSeq->release();
      mpSeq = pSeq;
      if (mpSeq)
        mpSeq->attach();
    }

    void setSelectedStep(int step) { mSelectedStep = step; }
    int getSelectedStep() { return mSelectedStep; }
    void setEditParam(od::Parameter *editGate) { mpEditGate = editGate; }

  private:
    GateSeq *mpSeq = 0;
    od::Parameter *mpEditGate = 0;
    int mSelectedStep = 0;
  };

} // namespace stolmine
