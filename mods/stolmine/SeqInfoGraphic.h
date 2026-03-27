#pragma once

#include <od/graphics/Graphic.h>
#include <TrackerSeq.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

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
      int right = mWorldLeft + mWidth - 3;

      char buf[16];

      // Row 1: spinner + step counter "04/16"
      // Spinner: rotating line based on playhead
      {
        int sx = mWorldLeft + 6;
        int sy = mWorldBottom + mHeight - 7;
        float angle = (float)playhead / (float)seqLen * 6.2832f;
        int dx = (int)(sinf(angle) * 4.0f);
        int dy = (int)(cosf(angle) * 4.0f);
        fb.circle(GRAY5, sx, sy, 4);
        fb.line(WHITE, sx, sy, sx + dx, sy + dy);
      }
      snprintf(buf, sizeof(buf), "%02d/%02d", playhead + 1, seqLen);
      fb.text(WHITE, right - getTextWidth(buf), mWorldBottom + mHeight - 12, buf, 10);

      // Row 2: loop indicator
      if (loopLen > 0)
        snprintf(buf, sizeof(buf), "lp:%d", loopLen);
      else
        snprintf(buf, sizeof(buf), "lp:off");
      fb.text(GRAY7, right - getTextWidth(buf) + 5, mWorldBottom + mHeight - 24, buf, 10);

      // Row 3: total tick length
      int totalTicks = mpSeq->getTotalTicks();
      snprintf(buf, sizeof(buf), "%dt", totalTicks);
      fb.text(GRAY7, right - getTextWidth(buf), mWorldBottom + mHeight - 36, buf, 10);

      // Row 4: transform state
      int lastFunc = mpSeq->getLastTransformFunc();
      if (lastFunc >= 0)
      {
        static const char *funcLabels[] = {
            "+", "-", "x", "/", "%%", "rev", "rot", "inv", "rnd"};
        static const char *scopeLabels[] = {"ofs", "len", "dev", "all"};
        int lastFactor = mpSeq->getLastTransformFactor();
        int lastScope = mpSeq->getLastTransformScope();
        const char *sl = scopeLabels[CLAMP(0, 3, lastScope)];
        if (lastFunc == XFORM_REVERSE || lastFunc == XFORM_INVERT)
          snprintf(buf, sizeof(buf), "%s:%s", funcLabels[lastFunc], sl);
        else
          snprintf(buf, sizeof(buf), "%s%d:%s", funcLabels[lastFunc], lastFactor, sl);
        fb.text(WHITE, right - getTextWidth(buf) + 3, mWorldBottom + mHeight - 48, buf, 10);
      }

      // Progress bar
      int barLeft = mWorldLeft + 2;
      int barRight = right;
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

    // Text width estimation for size-10 font (~5px per char)
    static int getTextWidth(const char *text)
    {
      int len = 0;
      while (text[len])
        len++;
      return len * 5;
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
