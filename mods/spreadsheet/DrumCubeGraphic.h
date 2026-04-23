#pragma once

#include <od/graphics/Graphic.h>
#include "DrumVoice.h"
#include <math.h>

namespace stolmine
{

  class DrumCubeGraphic : public od::Graphic
  {
  public:
    DrumCubeGraphic(int left, int bottom, int width, int height);
    virtual ~DrumCubeGraphic();

    void follow(DrumVoice *p);

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb);

  private:
    DrumVoice *mpDrum = nullptr;
    float mAngleX = 0.0f;
    float mAngleY = 0.3f;
    float mPunchEnergy = 0.0f;

    void fillTriangle(od::FrameBuffer &fb, int gray, int dotting, int faceIdx, bool gritNoise,
                      int x0, int y0, int x1, int y1, int x2, int y2);
    void fillQuad(od::FrameBuffer &fb, int gray, int dotting, int faceIdx, bool gritNoise,
                  int x0, int y0, int x1, int y1,
                  int x2, int y2, int x3, int y3);
#endif
  };

} // namespace stolmine
