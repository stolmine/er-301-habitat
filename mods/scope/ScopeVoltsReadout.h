// scope_unit::ScopeVoltsReadout
//
// Tiny header-only graphic that renders the rolling-mean voltage
// from a ScopeGraphic at draw time. Lives in the scope unit's
// sub-display (S3 column). Useful for DC and V/Oct analysis.
//
// All-inline per docs/graphics-authoring-guide.md.

#pragma once

#include "ScopeGraphic.h"
#include <od/graphics/FrameBuffer.h>
#include <od/graphics/Graphic.h>
#include <stdio.h>

namespace scope_unit
{

  class ScopeVoltsReadout : public od::Graphic
  {
  public:
    ScopeVoltsReadout(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpScope(0)
    {
    }

    virtual ~ScopeVoltsReadout()
    {
      if (mpScope) mpScope->release();
    }

    void follow(ScopeGraphic *p)
    {
      if (mpScope) mpScope->release();
      mpScope = p;
      if (mpScope) mpScope->attach();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      od::Graphic::draw(fb);

      char buf[16];
      float v = mpScope ? mpScope->getVolts() : 0.0f;
      // 3 decimal places; clamp display range to ER-301 ±10 V
      if (v >  9.9995f) v =  9.9995f;
      if (v < -9.9995f) v = -9.9995f;
      snprintf(buf, sizeof(buf), "%.3f", v);

      // Right-justify against the right edge of the readout region so
      // the decimal point sits at a consistent column as digits change.
      int textW = (int)strlen(buf) * 6; // approx px per char at size 10
      int tx = mWorldLeft + mWidth - textW - 2;
      int ty = mWorldBottom + (mHeight - 10) / 2;
      fb.text(WHITE, tx, ty, buf, 10);
    }

  private:
    ScopeGraphic *mpScope;
#endif
  };

} // namespace scope_unit
