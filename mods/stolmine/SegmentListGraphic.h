#pragma once

#include <od/graphics/Graphic.h>
#include <od/objects/Parameter.h>
#include <Etcher.h>
#include <string.h>
#include <stdio.h>

namespace stolmine
{
  class SegmentListGraphic : public od::Graphic
  {
  public:
    SegmentListGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~SegmentListGraphic()
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
      int activeSegment = mpEtcher->getActiveSegment();
      if (segCount < 1)
        return;

      const int rowHeight = 10;
      const int visibleRows = mHeight / rowHeight;

      // Keep selected segment visible
      if (mSelectedSegment < mScrollOffset)
        mScrollOffset = mSelectedSegment;
      if (mSelectedSegment >= mScrollOffset + visibleRows)
        mScrollOffset = mSelectedSegment - visibleRows + 1;
      if (mScrollOffset < 0)
        mScrollOffset = 0;

      for (int r = 0; r < visibleRows; r++)
      {
        int seg = mScrollOffset + r;
        if (seg >= segCount)
          break;

        int y = mWorldBottom + mHeight - (r + 1) * rowHeight;

        // Active segment highlight (where CV input currently lands)
        if (seg == activeSegment)
        {
          fb.fill(GRAY3, mWorldLeft, y, mWorldLeft + mWidth - 1, y + rowHeight - 1);
        }

        // Selected segment outline
        if (seg == mSelectedSegment)
        {
          fb.box(mFocused ? WHITE : GRAY5, mWorldLeft, y, mWorldLeft + mWidth - 1, y + rowHeight - 1);
        }

        // Segment number
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d", seg + 1);
        fb.text(GRAY7, mWorldLeft + 2, y + 1, buf, 10);

        // Offset value: show live edit buffer for selected segment
        float val = (seg == mSelectedSegment && mpEditOffset)
                        ? mpEditOffset->value()
                        : mpEtcher->getSegmentOffset(seg);
        snprintf(buf, sizeof(buf), "%+.1f", val);
        fb.text(WHITE, mWorldLeft + 18, y + 1, buf, 10);
      }

      // Scrollbar
      if (segCount > visibleRows)
      {
        int barHeight = MAX(2, mHeight * visibleRows / segCount);
        int barY = mWorldBottom + mHeight - barHeight
                   - (mHeight - barHeight) * mScrollOffset / (segCount - visibleRows);
        fb.vline(GRAY5, mWorldLeft + mWidth - 1, barY, barY + barHeight - 1, 0);
      }
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
    int getSelectedSegment() { return mSelectedSegment; }
    void setEditParam(od::Parameter *editOffset) { mpEditOffset = editOffset; }
    void setFocused(bool focused) { mFocused = focused; }

  private:
    Etcher *mpEtcher = 0;
    od::Parameter *mpEditOffset = 0;
    int mSelectedSegment = 0;
    int mScrollOffset = 0;
    bool mFocused = false;
  };

} // namespace stolmine
