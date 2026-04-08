#pragma once

#include <od/objects/Object.h>
#include <od/graphics/Graphic.h>
#include <od/config.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static const int kMaxBiquads = 7;

  class Sfera : public od::Object
  {
  public:
    Sfera();
    virtual ~Sfera();

    // SWIG-visible
    float getPoleAngle(int idx);
    float getPoleRadius(int idx);
    float getZeroAngle(int idx);
    float getZeroRadius(int idx);
    int getActiveSections();
    float getSphereRotation();
    int getNumCubes();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};

    od::Parameter mConfig{"Config", 0.0f};
    od::Parameter mParamX{"ParamX", 0.5f};
    od::Parameter mParamY{"ParamY", 0.5f};
    od::Parameter mCutoff{"Cutoff", 1000.0f};
    od::Parameter mQScale{"QScale", 1.0f};
    od::Parameter mLevel{"Level", 1.0f};

  private:
    struct Internal;
    Internal *mpInternal;
#endif
  };

  // Wireframe sphere with pole/zero deformation
  class SferaGraphic : public od::Graphic
  {
  public:
    SferaGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpSfera(0),
          mTargetAngle(0), mCurrentAngle(0) {}

    virtual ~SferaGraphic()
    {
      if (mpSfera)
        mpSfera->release();
    }

    void follow(Sfera *p)
    {
      if (mpSfera)
        mpSfera->release();
      mpSfera = p;
      if (mpSfera)
        mpSfera->attach();
    }

  private:
    Sfera *mpSfera;
    float mTargetAngle;
    float mCurrentAngle;

    static const int kLatLines = 6;
    static const int kLonLines = 8;
    static const int kPointsPerLine = 16;

    // Bounds-safe drawing helpers
    inline void safePixel(od::FrameBuffer &fb, int gray, int x, int y)
    {
      if (x >= mWorldLeft && x < mWorldLeft + mWidth &&
          y >= mWorldBottom && y < mWorldBottom + mHeight)
        fb.pixel(gray, x, y);
    }

    inline void safeLine(od::FrameBuffer &fb, int gray, int x0, int y0, int x1, int y1)
    {
      // Simple clip: skip if both endpoints are far out of bounds
      int left = mWorldLeft, right = mWorldLeft + mWidth - 1;
      int bot = mWorldBottom, top = mWorldBottom + mHeight - 1;
      if ((x0 < left - 10 && x1 < left - 10) || (x0 > right + 10 && x1 > right + 10)) return;
      if ((y0 < bot - 10 && y1 < bot - 10) || (y0 > top + 10 && y1 > top + 10)) return;
      fb.line(gray, x0, y0, x1, y1);
    }

  public:
    virtual void draw(od::FrameBuffer &fb)
    {
      int w = mWidth;
      int h = mHeight;
      int cx = mWorldLeft + w / 2;
      int cy = mWorldBottom + h / 2;
      float rad = (float)(w < h ? w : h) * 0.42f;

      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + w - 1, mWorldBottom + h - 1);

      if (!mpSfera) return;

      // Smooth rotation
      mTargetAngle = mpSfera->getSphereRotation();
      float diff = mTargetAngle - mCurrentAngle;
      while (diff > 3.14159f) diff -= 6.28318f;
      while (diff < -3.14159f) diff += 6.28318f;
      mCurrentAngle += diff * 0.08f;

      float cosR = cosf(mCurrentAngle);
      float sinR = sinf(mCurrentAngle);
      float tiltCos = 0.9659f;
      float tiltSin = 0.2588f;

      int nSections = mpSfera->getActiveSections();
      if (nSections > 7) nSections = 7;

      // --- 1. Draw sphere outline (thin equator ellipse) ---
      {
        int prevPx = -1, prevPy = -1;
        for (int p = 0; p <= 24; p++)
        {
          float a = (float)p * 6.28318f / 24.0f;
          float x3 = cosf(a);
          float z3 = sinf(a);
          float rx = x3 * cosR + z3 * sinR;
          float rz = -x3 * sinR + z3 * cosR;
          float ry = -rz * tiltSin;
          int ppx = cx + (int)(rx * rad);
          int ppy = cy + (int)(ry * rad);
          if (prevPx >= 0)
            safeLine(fb, GRAY3, prevPx, prevPy, ppx, ppy);
          prevPx = ppx;
          prevPy = ppy;
        }
        // Vertical meridian
        prevPx = -1;
        for (int p = 0; p <= 24; p++)
        {
          float a = (float)p * 6.28318f / 24.0f;
          float y3 = cosf(a);
          float z3 = sinf(a);
          float rx = z3 * sinR;
          float rz = z3 * cosR;
          float ry = y3 * tiltCos - rz * tiltSin;
          int ppx = cx + (int)(rx * rad);
          int ppy = cy + (int)(ry * rad);
          if (prevPx >= 0)
            safeLine(fb, GRAY2, prevPx, prevPy, ppx, ppy);
          prevPx = ppx;
          prevPy = ppy;
        }
      }

      // --- 2. Draw pole pom-poms (radial gradient spheres) ---
      for (int i = 0; i < nSections; i++)
      {
        float pa = mpSfera->getPoleAngle(i);
        float pr = mpSfera->getPoleRadius(i);
        if (pr < 0.01f) continue;

        // 3D position on equator at angle pa
        float x3 = cosf(pa);
        float z3 = sinf(pa);
        float rx = x3 * cosR + z3 * sinR;
        float rz = -x3 * sinR + z3 * cosR;
        float ry = -rz * tiltSin;

        // Depth for brightness attenuation (back-facing dimmer)
        float depth = rz * tiltCos; // roughly how far "back" this point is
        float depthFade = 0.5f + 0.5f * (1.0f - depth); // 0.5 at back, 1.0 at front
        if (depthFade < 0.3f) depthFade = 0.3f;

        int bx = cx + (int)(rx * rad);
        int by = cy + (int)(ry * rad);

        // Pom-pom radius: proportional to pole influence, 3-12 pixels
        int pomRad = 3 + (int)(pr * 10.0f);
        if (pomRad > 14) pomRad = 14;

        int peakBright = (int)(10.0f * depthFade);
        if (peakBright > 13) peakBright = 13;

        // Draw filled radial gradient
        for (int dy = -pomRad; dy <= pomRad; dy++)
        {
          for (int dx = -pomRad; dx <= pomRad; dx++)
          {
            float d2 = (float)(dx * dx + dy * dy);
            float r2 = (float)(pomRad * pomRad);
            if (d2 > r2) continue;

            // Radial falloff: bright center, fades to edge
            float t = 1.0f - d2 / r2; // 1 at center, 0 at edge
            t = t * t; // sharper falloff
            int gray = (int)((float)peakBright * t);
            if (gray < 1) continue;

            safePixel(fb, gray, bx + dx, by + dy);
          }
        }
      }

      // --- 3. Draw zero dimples (dark spots with faint ring) ---
      for (int i = 0; i < nSections; i++)
      {
        float za = mpSfera->getZeroAngle(i);
        float zr = mpSfera->getZeroRadius(i);
        if (zr < 0.01f) continue;

        float x3 = cosf(za);
        float z3 = sinf(za);
        float rx = x3 * cosR + z3 * sinR;
        float rz = -x3 * sinR + z3 * cosR;
        float ry = -rz * tiltSin;

        int bx = cx + (int)(rx * rad);
        int by = cy + (int)(ry * rad);

        int zRad = 2 + (int)(zr * 4.0f);
        if (zRad > 6) zRad = 6;

        // Dark center, faint ring at edge
        for (int dy = -zRad; dy <= zRad; dy++)
        {
          for (int dx = -zRad; dx <= zRad; dx++)
          {
            float d2 = (float)(dx * dx + dy * dy);
            float r2 = (float)(zRad * zRad);
            if (d2 > r2) continue;

            float t = d2 / r2; // 0 at center, 1 at edge
            // Ring: bright at edge, dark at center
            int gray = (int)(3.0f * t * t);
            if (gray < 1) gray = 1;

            safePixel(fb, gray, bx + dx, by + dy);
          }
        }
      }
    }
  };

} // namespace stolmine
