#pragma once

#include <od/graphics/Graphic.h>
#include <Etcher.h>
#include <math.h>

namespace stolmine
{
  class TransferCurveGraphic : public od::Graphic
  {
  public:
    TransferCurveGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~TransferCurveGraphic()
    {
      if (mpEtcher)
        mpEtcher->release();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpEtcher)
        return;

      int segCount = mpEtcher->getSegmentCount();

      // Right boundary line
      fb.vline(GRAY5, mWorldLeft + mWidth - 1, mWorldBottom, mWorldBottom + mHeight - 1, 0);

      // Draw segment boundary lines
      for (int i = 1; i < segCount; i++)
      {
        float b = mpEtcher->getSegmentBoundary(i);
        int x = mWorldLeft + (int)(b * (float)(mWidth - 1));
        // Highlight selected segment boundaries
        if (i == mSelectedSegment || i == mSelectedSegment + 1)
          fb.vline(GRAY7, x, mWorldBottom, mWorldBottom + mHeight - 1, 0);
        else
          fb.vline(GRAY3, x, mWorldBottom, mWorldBottom + mHeight - 1, 0);
      }

      // Highlight selected segment span
      if (mSelectedSegment >= 0 && mSelectedSegment < segCount)
      {
        float bStart = mpEtcher->getSegmentBoundary(mSelectedSegment);
        float bEnd = mpEtcher->getSegmentBoundary(mSelectedSegment + 1);
        int xStart = mWorldLeft + (int)(bStart * (float)(mWidth - 1));
        int xEnd = mWorldLeft + (int)(bEnd * (float)(mWidth - 1));
        fb.fill(GRAY1, xStart, mWorldBottom, xEnd, mWorldBottom + mHeight - 1);
      }

      // Draw transfer function polyline
      int prevX = mWorldLeft;
      int prevY = mapOutputToY(mpEtcher->evaluate(0.0f));

      for (int px = 1; px < mWidth; px++)
      {
        float normalizedInput = (float)px / (float)(mWidth - 1);
        float output = mpEtcher->evaluate(normalizedInput);
        int x = mWorldLeft + px;
        int y = mapOutputToY(output);
        fb.line(WHITE, prevX, prevY, x, y);
        prevX = x;
        prevY = y;
      }

      // Draw playhead
      float currentInput = mpEtcher->getCurrentInput();
      float normalizedPos = (currentInput + 1.0f) * 0.5f;
      normalizedPos = CLAMP(0.0f, 1.0f, normalizedPos);
      int playX = mWorldLeft + (int)(normalizedPos * (float)(mWidth - 1));

      // Vertical hairline
      fb.vline(GRAY7, playX, mWorldBottom, mWorldBottom + mHeight - 1, 2);

      // Dot on curve at current position
      float currentOutput = mpEtcher->getCurrentOutput();
      int dotY = mapOutputToY(currentOutput);
      fb.circle(WHITE, playX, dotY, 2);
    }

    int mapOutputToY(float output)
    {
      // Map -5..+5 to bottom..top of widget
      float normalized = (output + 1.0f) * 0.5f;
      normalized = CLAMP(0.0f, 1.0f, normalized);
      return mWorldBottom + (int)(normalized * (float)(mHeight - 1));
    }
#endif

    void follow(Etcher *pEtcher)
    {
      if (mpEtcher)
        mpEtcher->release();
      mpEtcher = pEtcher;
      if (mpEtcher)
        mpEtcher->attach();
    }

    void setSelectedSegment(int seg) { mSelectedSegment = seg; }

  private:
    Etcher *mpEtcher = 0;
    int mSelectedSegment = 0;
  };

} // namespace stolmine
