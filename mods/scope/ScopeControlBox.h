// scope_unit::ScopeControlBox
//
// Tiny header-only graphic that renders a rectangular border around
// its own area. Solid when "focused" (the user is actively editing
// the wrapped value), dotted otherwise. Sits as a sibling of the
// Label / value graphic at the same coordinates, drawn first so the
// content renders on top.
//
// All-inline per docs/graphics-authoring-guide.md.

#pragma once

#include <od/graphics/FrameBuffer.h>
#include <od/graphics/Graphic.h>

namespace scope_unit
{

  class ScopeControlBox : public od::Graphic
  {
  public:
    ScopeControlBox(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mFocused(false)
    {
    }

    virtual ~ScopeControlBox() {}

    void setFocused(bool b)
    {
      mFocused = b;
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      od::Graphic::draw(fb);

      int dotting = mFocused ? 0 : 2; // 0 = solid, 2 = every-other-pixel
      int x0 = mWorldLeft;
      int x1 = mWorldLeft + mWidth - 1;
      int y0 = mWorldBottom;
      int y1 = mWorldBottom + mHeight - 1;

      fb.hline(WHITE, x0, x1, y0, dotting);
      fb.hline(WHITE, x0, x1, y1, dotting);
      fb.vline(WHITE, x0, y0, y1, dotting);
      fb.vline(WHITE, x1, y0, y1, dotting);
    }

  private:
    bool mFocused;
#endif
  };

} // namespace scope_unit
