#pragma once

#include <od/graphics/sampling/TapeHeadDisplay.h>
#include "GestureSeq.h"

namespace stolmine
{
  class GestureHeadDisplay : public od::TapeHeadDisplay
  {
  public:
    GestureHeadDisplay(GestureSeq *head, int left, int bottom, int width, int height)
        : od::TapeHeadDisplay(head, left, bottom, width, height)
    {
      mSampleView.setViewTimeInSeconds(5);
    }

    virtual ~GestureHeadDisplay() {}
  };

} // namespace stolmine
