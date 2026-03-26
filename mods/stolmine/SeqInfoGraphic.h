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

      // Step counter: "04/16"
      char buf[12];
      snprintf(buf, sizeof(buf), "%02d/%02d", playhead + 1, seqLen);
      fb.text(WHITE, mWorldLeft + 2, mWorldBottom + mHeight - 12, buf, 10);

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

      // Label
      fb.text(GRAY7, mWorldLeft + 2, mWorldBottom + mHeight - 24, "seq", 10);
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
