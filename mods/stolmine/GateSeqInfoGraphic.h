#pragma once

#include <od/graphics/Graphic.h>
#include <od/objects/Parameter.h>
#include <GateSeq.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace stolmine
{
  class GateSeqInfoGraphic : public od::Graphic
  {
  public:
    GateSeqInfoGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~GateSeqInfoGraphic()
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

      // Row 1: spinner + step counter
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
            "euc", "nr", "grd", "nkl", "inv", "rot", "den"};
        int lastParamA = mpSeq->getLastTransformParamA();
        int lastParamB = mpSeq->getLastTransformParamB();
        if (lastFunc == GS_XFORM_INVERT)
          snprintf(buf, sizeof(buf), "%s", funcLabels[CLAMP(0, 6, lastFunc)]);
        else if (lastFunc == GS_XFORM_ROTATE || lastFunc == GS_XFORM_DENSITY)
          snprintf(buf, sizeof(buf), "%s %d", funcLabels[CLAMP(0, 6, lastFunc)], lastParamA);
        else
          snprintf(buf, sizeof(buf), "%s %d:%d", funcLabels[CLAMP(0, 6, lastFunc)], lastParamA, lastParamB);
        fb.text(WHITE, right - getTextWidth(buf) + 3, mWorldBottom + mHeight - 48, buf, 10);
      }

      // Width readout
      if (mpWidthParam)
      {
        int widthPct = (int)(mpWidthParam->value() * 100.0f + 0.5f);
        snprintf(buf, sizeof(buf), "w:%d%%", widthPct);
        fb.text(GRAY7, right - getTextWidth(buf), mWorldBottom + 2, buf, 10);
      }
    }

    static int getTextWidth(const char *text)
    {
      int len = 0;
      while (text[len])
        len++;
      return len * 5;
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

    void setWidthParam(od::Parameter *p) { mpWidthParam = p; }

  private:
    GateSeq *mpSeq = 0;
    od::Parameter *mpWidthParam = 0;
  };

} // namespace stolmine
