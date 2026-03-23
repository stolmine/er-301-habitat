#pragma once

#include <od/graphics/Graphic.h>
#include <Grids.h>
#include <math.h>

namespace grids
{
  class GridsCircle : public od::Graphic
  {
  public:
    GridsCircle(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~GridsCircle()
    {
      if (mpGrids)
        mpGrids->release();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      const int MARGIN = 2;
      const int CURSOR = 3;
      const int LENGTH = 32;

      int radius = MIN(mWidth / 2, mHeight / 2) - MARGIN - CURSOR;
      float outerRadius = radius;
      float innerRadius = radius * 0.75f;

      int x = mWorldLeft + mWidth / 2;
      int y = mWorldBottom + mHeight / 2;

      if (!mpGrids)
        return;

      int step = mpGrids->getStep();

      float arcWidth = M_PI * 2.0f / (float)LENGTH;
      float halfArc = arcWidth / 2.0f;

      // Filled arcs for active steps
      float fillRadius = outerRadius * 0.985f;
      for (int i = 0; i < LENGTH; i++)
      {
        if (!mpGrids->isSet(i))
          continue;
        float theta = arcWidth * (float)i;
        fillArc(fb, GRAY5, x, y, fillRadius, theta - halfArc, theta + halfArc);
      }

      // Clear center
      fb.fillCircle(BLACK, x, y, innerRadius);

      // Ring outlines
      fb.circle(GRAY10, x, y, radius);
      fb.circle(GRAY10, x, y, innerRadius);

      // Step marker (show last-played step)
      float markerRadius = radius * 0.125f;
      float markerOffset = innerRadius - markerRadius;
      int displayStep = ((step - 1) % LENGTH + LENGTH) % LENGTH;
      float theta = arcWidth * (float)displayStep;
      float xOff = sinf(theta) * markerOffset + x;
      float yOff = cosf(theta) * markerOffset + y;

      fb.fillCircle(GRAY8, xOff, yOff, markerRadius);
      fb.circle(WHITE, xOff, yOff, markerRadius);
    }
#endif

    void follow(Grids *pGrids)
    {
      if (mpGrids)
        mpGrids->release();
      mpGrids = pGrids;
      if (mpGrids)
        mpGrids->attach();
    }

  private:
    Grids *mpGrids = 0;

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
} // namespace grids
