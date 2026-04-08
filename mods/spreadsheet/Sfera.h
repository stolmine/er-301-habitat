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
      float radX = (float)w * 0.40f;
      float radY = (float)h * 0.40f;

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

      // --- Project pole/zero positions to screen ---
      float poleSx[7], poleSy[7], poleDf[7]; // screen x, y, depth fade
      float zeroSx[7], zeroSy[7];
      for (int i = 0; i < nSections; i++)
      {
        float pa = mpSfera->getPoleAngle(i);
        float x3 = cosf(pa), z3 = sinf(pa);
        float rx = x3 * cosR + z3 * sinR;
        float rz = -x3 * sinR + z3 * cosR;
        float ry = -rz * tiltSin;
        poleSx[i] = rx;
        poleSy[i] = ry;
        poleDf[i] = 0.4f + 0.6f * (0.5f + 0.5f * (rz * tiltCos)); // depth fade

        float za = mpSfera->getZeroAngle(i);
        float zr = mpSfera->getZeroRadius(i);
        if (zr > 0.01f)
        {
          float zx3 = cosf(za), zz3 = sinf(za);
          float zrx = zx3 * cosR + zz3 * sinR;
          float zrz = -zx3 * sinR + zz3 * cosR;
          zeroSx[i] = zrx;
          zeroSy[i] = -zrz * tiltSin;
        }
        else { zeroSx[i] = 0; zeroSy[i] = 0; }
      }

      // --- Metaball field: for each pixel, evaluate field from all poles ---
      // Field = sum(strength / distance^2) for each pole
      // Pixels where field > threshold are "inside" the metaball
      // Brightness = field intensity above threshold

      float invRx = 1.0f / (radX > 1 ? radX : 1);
      float invRy = 1.0f / (radY > 1 ? radY : 1);

      for (int py = 0; py < h; py++)
      {
        // Normalized y in [-1, 1]
        float ny = ((float)py - (float)h * 0.5f) * invRy;

        for (int px = 0; px < w; px++)
        {
          float nx = ((float)px - (float)w * 0.5f) * invRx;

          // Skip pixels outside sphere outline (with margin for escaping blobs)
          float sphereDist2 = nx * nx + ny * ny;

          // Metaball field: sum contributions from all active poles
          float field = 0.0f;
          for (int i = 0; i < nSections; i++)
          {
            float pr = mpSfera->getPoleRadius(i);
            if (pr < 0.01f) continue;

            float dx = nx - poleSx[i];
            float dy = ny - poleSy[i];
            float d2 = dx * dx + dy * dy + 0.01f; // avoid div by zero
            float strength = pr * 0.15f * poleDf[i];
            field += strength / d2;
          }

          // Subtract zero contributions (create holes)
          for (int i = 0; i < nSections; i++)
          {
            float zr = mpSfera->getZeroRadius(i);
            if (zr < 0.01f) continue;

            float dx = nx - zeroSx[i];
            float dy = ny - zeroSy[i];
            float d2 = dx * dx + dy * dy + 0.02f;
            field -= zr * 0.05f / d2;
          }

          if (field < 0.3f) continue; // below threshold, skip

          // Sphere shell: faint outline at r~1
          float shell = 0.0f;
          if (sphereDist2 > 0.85f && sphereDist2 < 1.0f)
            shell = (sphereDist2 - 0.85f) / 0.15f * 2.0f;

          // Metaball brightness: field above threshold, capped
          float blob = (field - 0.3f) * 8.0f;
          if (blob > 12.0f) blob = 12.0f;

          int gray = (int)(blob + shell);
          if (gray > 13) gray = 13;
          if (gray < 1) continue;

          safePixel(fb, gray, mWorldLeft + px, mWorldBottom + py);
        }
      }
    }
  };

} // namespace stolmine
