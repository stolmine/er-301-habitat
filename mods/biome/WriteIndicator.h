#pragma once

#include <od/graphics/Graphic.h>
#include <od/objects/Option.h>

namespace stolmine
{
  class WriteIndicator : public od::Graphic
  {
  public:
    WriteIndicator(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~WriteIndicator()
    {
      if (mpOption)
        mpOption->release();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpOption)
        return;

      int cx = mWorldLeft + mWidth / 3;
      int cy = mWorldBottom + mHeight / 2;
      int r = 12;

      bool active = mpOption->value() == 1;

      if (active)
      {
        // Filled diamond via horizontal scanlines
        for (int dy = -r; dy <= r; dy++)
        {
          int half = r - (dy < 0 ? -dy : dy);
          if (half > 0)
            fb.hline(WHITE, cx - half, cx + half, cy + dy);
        }
      }
      else
      {
        // Diamond outline
        fb.line(GRAY5, cx, cy + r, cx + r, cy);
        fb.line(GRAY5, cx + r, cy, cx, cy - r);
        fb.line(GRAY5, cx, cy - r, cx - r, cy);
        fb.line(GRAY5, cx - r, cy, cx, cy + r);
      }
    }
#endif

    void setOption(od::Option *option)
    {
      if (mpOption)
        mpOption->release();
      mpOption = option;
      if (mpOption)
        mpOption->attach();
    }

  private:
    od::Option *mpOption = nullptr;
  };

} // namespace stolmine
