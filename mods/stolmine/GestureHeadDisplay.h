#pragma once

#include <od/graphics/sampling/TapeHeadDisplay.h>
#include <od/audio/Sample.h>
#include "GestureSeq.h"

namespace stolmine
{
  class GestureHeadDisplay : public od::TapeHeadDisplay
  {
  public:
    GestureHeadDisplay(GestureSeq *head, int left, int bottom, int width, int height)
        : od::TapeHeadDisplay(head, left, bottom, width, height)
    {
    }

    virtual ~GestureHeadDisplay() {}

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      uint32_t pos = 0;

      if (mpHead)
      {
        od::Sample *pSample = mpHead->getSample();
        pos = mpHead->getPosition();

        if (mSampleView.setSample(pSample))
        {
          // New sample attached
        }
        else if (mLastPosition != pos)
        {
          // Invalidate the region written since last draw
          if (mLastPosition < pos)
          {
            mSampleView.invalidateInterval(mLastPosition, pos);
          }
          else if (pSample)
          {
            mSampleView.invalidateInterval(mLastPosition, pSample->mSampleCount);
            mSampleView.invalidateInterval(0, pos);
          }
        }

        mLastPosition = pos;
      }

      mSampleView.setCenterPosition(pos);
      mSampleView.draw(fb);
      mSampleView.drawPositionOverview(fb, pos);
      mSampleView.drawPosition(fb, pos, "P", 6);
      drawStatus(fb);

      switch (mZoomGadgetState)
      {
      case showTimeGadget:
        mSampleView.drawTimeZoomGadget(fb);
        mCursorState.copyAttributes(mSampleView.mCursorState);
        break;
      case showGainGadget:
        mSampleView.drawGainZoomGadget(fb);
        mCursorState.copyAttributes(mSampleView.mCursorState);
        break;
      default:
        break;
      }
    }

    uint32_t mLastPosition = 0;
#endif
  };

} // namespace stolmine
