#pragma once

#include <od/graphics/Graphic.h>
#include <TrackerSeq.h>
#include <stdio.h>

namespace stolmine
{
  class SeqInfoGraphic : public od::Graphic
  {
  public:
    SeqInfoGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~SeqInfoGraphic()
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
      int loopLen = mpSeq->getLoopLength();

      // Step counter: "04/16"
      char buf[16];
      snprintf(buf, sizeof(buf), "%02d/%02d", playhead + 1, seqLen);
      fb.text(WHITE, mWorldLeft + 2, mWorldBottom + mHeight - 12, buf, 10);

      // Loop indicator
      if (loopLen > 0)
      {
        snprintf(buf, sizeof(buf), "lp:%d", loopLen);
      }
      else
      {
        snprintf(buf, sizeof(buf), "lp:off");
      }
      fb.text(GRAY7, mWorldLeft + 2, mWorldBottom + mHeight - 24, buf, 10);

      // Total tick length
      int totalTicks = mpSeq->getTotalTicks();
      snprintf(buf, sizeof(buf), "%dt", totalTicks);
      fb.text(GRAY7, mWorldLeft + 2, mWorldBottom + mHeight - 36, buf, 10);

      // Progress bar
      int barLeft = mWorldLeft + 2;
      int barRight = mWorldLeft + mWidth - 3;
      int barBottom = mWorldBottom + 4;
      int barTop = barBottom + 6;

      fb.box(GRAY5, barLeft, barBottom, barRight, barTop);

      if (seqLen > 0)
      {
        int fillWidth = (barRight - barLeft - 1) * (playhead + 1) / seqLen;
        if (fillWidth > 0)
        {
          fb.fill(GRAY10, barLeft + 1, barBottom + 1,
                  barLeft + fillWidth, barTop - 1);
        }
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

  private:
    TrackerSeq *mpSeq = 0;
  };

} // namespace stolmine
