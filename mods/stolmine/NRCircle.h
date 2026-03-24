#pragma once

#include <od/graphics/Graphic.h>
#include <NR.h>
#include <math.h>

namespace nr
{
  class NRCircle : public od::Graphic
  {
  public:
    NRCircle(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~NRCircle()
    {
      if (mpNR)
        mpNR->release();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      const int MARGIN = 2;
      const int CURSOR = 3;

      int radius = MIN(mWidth / 2, mHeight / 2) - MARGIN - CURSOR;
      float outerRadius = radius;
      float innerRadius = radius * 0.75f;

      int x = mWorldLeft + mWidth / 2;
      int y = mWorldBottom + mHeight / 2;

      if (!mpNR)
        return;

      int length = mpNR->getLength();
      int step = mpNR->getStep();
      if (length < 1)
        return;

      float arcWidth = M_PI * 2.0f / (float)length;
      float halfArc = arcWidth / 2.0f;

      // Filled arcs for active steps
      float fillRadius = outerRadius * 0.985f;
      for (int i = 0; i < length; i++)
      {
        if (!mpNR->isSet(i))
          continue;
        float theta = arcWidth * (float)i;
        fillArc(fb, GRAY5, x, y, fillRadius, theta - halfArc, theta + halfArc);
      }

      // Delineation lines
      float delineateRadius = outerRadius * 0.98f;
      float delineateColor = WHITE * (64.0f - (float)length) / 64.0f;
      for (int i = 0; i < length; i++)
      {
        float theta = arcWidth * (float)i;
        drawRadius(fb, delineateColor, x, y, delineateRadius, theta - halfArc);
      }

      // Clear center
      fb.fillCircle(BLACK, x, y, innerRadius);

      // Ring outlines
      fb.circle(GRAY10, x, y, radius);
      fb.circle(GRAY10, x, y, innerRadius);

      // Step marker (show last-played step)
      float markerRadius = radius * 0.125f;
      float markerOffset = innerRadius - markerRadius;
      int displayStep = ((step - 1) % length + length) % length;
      float theta = arcWidth * (float)displayStep;
      float xOff = sinf(theta) * markerOffset + x;
      float yOff = cosf(theta) * markerOffset + y;

      fb.fillCircle(GRAY8, xOff, yOff, markerRadius);
      fb.circle(WHITE, xOff, yOff, markerRadius);
    }
#endif

    void follow(NR *pNR)
    {
      if (mpNR)
        mpNR->release();
      mpNR = pNR;
      if (mpNR)
        mpNR->attach();
    }

  private:
    NR *mpNR = 0;

    void drawRadius(od::FrameBuffer &fb, od::Color color,
                    float x, float y, float radius, float theta)
    {
      int toX = sinf(theta) * radius + x;
      int toY = cosf(theta) * radius + y;
      fb.line(color, x, y, toX, toY);
    }

    void fillArc(od::FrameBuffer &fb, od::Color color,
                 float x, float y, float radius,
                 float theta0, float theta1)
    {
      float dTheta = fabs(theta1 - theta0);
      float arcLength = ceil(fabs(radius * dTheta));
      float resolution = arcLength * 16.0f;
      for (float i = 0; i < resolution; i++)
      {
        float offset = dTheta * i / resolution;
        drawRadius(fb, color, x, y, radius, theta0 + offset);
      }
    }
  };
} // namespace nr
