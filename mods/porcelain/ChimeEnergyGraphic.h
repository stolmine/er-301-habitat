// ChimeEnergyGraphic -- vertical bars per resonator with coupling hints.
// Axis-aligned only; no sinf/cosf at runtime (cf. memory feedback_package_trig_lut).

#pragma once

#include <od/graphics/Graphic.h>
#include <Chime.h>

namespace stolmine
{
  class ChimeEnergyGraphic : public od::Graphic
  {
  public:
    ChimeEnergyGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~ChimeEnergyGraphic()
    {
      if (mpChime) mpChime->release();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      if (!mpChime) return;
      int bandCount = mpChime->getBandCount();
      if (bandCount < 1) return;

      int left   = mWorldLeft + 2;
      int right  = mWorldLeft + mWidth - 2;
      int bottom = mWorldBottom + 2;
      int top    = mWorldBottom + mHeight - 2;
      int w = right - left;
      int h = top - bottom;

      // Slot width per band. Bars leave a 1-px gap on each side.
      float slotW = (float)w / (float)bandCount;
      int barW = (int)(slotW - 2);
      if (barW < 1) barW = 1;

      // Normalize envelopes by the loudest so the tallest band always reaches top.
      float maxEnv = 0.001f;
      for (int b = 0; b < bandCount; b++)
      {
        float e = mpChime->getBandEnergy(b);
        if (e > maxEnv) maxEnv = e;
      }

      // Draw coupling ribbons first so they sit under the bars.
      float couple = mpChime->getCouple();
      int coupleShade = 1 + (int)(couple * 10.0f);
      if (coupleShade > 12) coupleShade = 12;

      for (int b = 0; b < bandCount - 1; b++)
      {
        float e0 = mpChime->getBandEnergy(b) / maxEnv;
        float e1 = mpChime->getBandEnergy(b + 1) / maxEnv;
        float eAvg = (e0 + e1) * 0.5f;
        int y0 = bottom + (int)(eAvg * (float)h);
        // Link the two adjacent bar tops with a thin line.
        int cx0 = left + (int)((b + 0.5f) * slotW);
        int cx1 = left + (int)((b + 1.5f) * slotW);
        fb.line(coupleShade, cx0, y0, cx1, y0);
      }

      // Draw bars on top.
      for (int b = 0; b < bandCount; b++)
      {
        float e = mpChime->getBandEnergy(b) / maxEnv;
        if (e > 1.0f) e = 1.0f;
        int barH = (int)(e * (float)h);
        if (barH < 1) barH = 1;

        int cx = left + (int)((b + 0.5f) * slotW);
        int x0 = cx - barW / 2;
        int x1 = x0 + barW;
        int y0 = bottom;
        int y1 = bottom + barH;

        // Faint outline so empty bars still read as slots.
        fb.box(GRAY3, x0, y0, x1, top);
        fb.fill(WHITE, x0, y0, x1, y1);
      }
    }
#endif

    void follow(Chime *pChime)
    {
      if (mpChime) mpChime->release();
      mpChime = pChime;
      if (mpChime) mpChime->attach();
    }

  private:
    Chime *mpChime = 0;
  };

} // namespace stolmine
