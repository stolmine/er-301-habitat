#pragma once

#include <od/graphics/Graphic.h>
#include <od/objects/Parameter.h>
#include <TrackerSeq.h>
#include <string.h>
#include <stdio.h>

namespace stolmine
{
  class StepListGraphic : public od::Graphic
  {
  public:
    StepListGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~StepListGraphic()
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
      if (seqLen < 1)
        return;

      const int rowHeight = 10;
      const int visibleRows = mHeight / rowHeight;

      // Keep selected step visible
      if (mSelectedStep < mScrollOffset)
        mScrollOffset = mSelectedStep;
      if (mSelectedStep >= mScrollOffset + visibleRows)
        mScrollOffset = mSelectedStep - visibleRows + 1;
      if (mScrollOffset < 0)
        mScrollOffset = 0;

      for (int r = 0; r < visibleRows; r++)
      {
        int step = mScrollOffset + r;
        if (step >= seqLen)
          break;

        int y = mWorldBottom + mHeight - (r + 1) * rowHeight;

        // Playhead highlight
        if (step == playhead)
        {
          fb.fill(GRAY3, mWorldLeft, y, mWorldLeft + mWidth - 1, y + rowHeight - 1);
        }

        // Selected step outline
        if (step == mSelectedStep)
        {
          fb.box(mFocused ? WHITE : GRAY5, mWorldLeft, y, mWorldLeft + mWidth - 1, y + rowHeight - 1);
        }

        // Step number
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d", step + 1);
        fb.text(GRAY7, mWorldLeft + 2, y + 1, buf, 10);

        // Offset value: show live edit buffer for selected step
        float val = (step == mSelectedStep && mpEditOffset)
            ? mpEditOffset->value()
            : mpSeq->getStepOffset(step);
        snprintf(buf, sizeof(buf), "%+.1f", val);
        fb.text(WHITE, mWorldLeft + 18, y + 1, buf, 10);
      }

      // Scrollbar
      if (seqLen > visibleRows)
      {
        int barHeight = MAX(2, mHeight * visibleRows / seqLen);
        int barY = mWorldBottom + mHeight - barHeight
                   - (mHeight - barHeight) * mScrollOffset / (seqLen - visibleRows);
        fb.vline(GRAY5, mWorldLeft + mWidth - 1, barY, barY + barHeight - 1, 0);
      }
    }
#endif

    void follow(TrackerSeq *pSeq)
    {
      if (mpSeq)
        mpSeq->release();
      mpSeq = pSeq;
      if (mpSeq)
        mpSeq->attach();
    }

    void setSelectedStep(int step) { mSelectedStep = step; }
    int getSelectedStep() { return mSelectedStep; }
    void setEditParam(od::Parameter *editOffset) { mpEditOffset = editOffset; }
    void setFocused(bool focused) { mFocused = focused; }

  private:
    TrackerSeq *mpSeq = 0;
    od::Parameter *mpEditOffset = 0;
    int mSelectedStep = 0;
    int mScrollOffset = 0;
    bool mFocused = false;
  };

} // namespace stolmine
