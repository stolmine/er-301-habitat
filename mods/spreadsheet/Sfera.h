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
      float radius = (float)(w < h ? w : h) * 0.42f;

      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + w - 1, mWorldBottom + h - 1);

      if (!mpSfera) return;

      // Smooth rotation toward target
      mTargetAngle = mpSfera->getSphereRotation();
      float diff = mTargetAngle - mCurrentAngle;
      // Wrap to [-pi, pi]
      while (diff > 3.14159f) diff -= 6.28318f;
      while (diff < -3.14159f) diff += 6.28318f;
      mCurrentAngle += diff * 0.08f;

      float cosR = cosf(mCurrentAngle);
      float sinR = sinf(mCurrentAngle);

      // Slight tilt for 3D feel
      float tiltCos = 0.9659f; // cos(15 deg)
      float tiltSin = 0.2588f; // sin(15 deg)

      // Get active pole/zero state
      int nSections = mpSfera->getActiveSections();
      float poleAng[7], poleRad[7], zeroAng[7], zeroRad[7];
      for (int i = 0; i < nSections; i++)
      {
        poleAng[i] = mpSfera->getPoleAngle(i);
        poleRad[i] = mpSfera->getPoleRadius(i);
        zeroAng[i] = mpSfera->getZeroAngle(i);
        zeroRad[i] = mpSfera->getZeroRadius(i);
      }

      // Draw longitude lines (meridians)
      for (int lon = 0; lon < kLonLines; lon++)
      {
        float lonAngle = (float)lon * 6.28318f / (float)kLonLines;
        int prevPx = -1, prevPy = -1;
        for (int p = 0; p <= kPointsPerLine; p++)
        {
          float lat = -1.5708f + (float)p * 3.14159f / (float)kPointsPerLine;
          float r = 1.0f;

          // Deform by poles (push out) and zeros (pull in)
          float sx = cosf(lat) * cosf(lonAngle);
          float sy = sinf(lat);
          float sz = cosf(lat) * sinf(lonAngle);
          for (int i = 0; i < nSections; i++)
          {
            // Map pole angle to sphere position
            float pa = poleAng[i];
            float px3 = cosf(0) * cosf(pa); // pole on equator at angle pa
            float pz3 = cosf(0) * sinf(pa);
            float dist = (sx - px3) * (sx - px3) + sy * sy + (sz - pz3) * (sz - pz3);
            float influence = poleRad[i] * 0.3f / (dist + 0.1f);
            r += influence;

            if (zeroRad[i] > 0.01f)
            {
              float za = zeroAng[i];
              float zx3 = cosf(0) * cosf(za);
              float zz3 = cosf(0) * sinf(za);
              float zdist = (sx - zx3) * (sx - zx3) + sy * sy + (sz - zz3) * (sz - zz3);
              float zinfluence = zeroRad[i] * 0.2f / (zdist + 0.1f);
              r -= zinfluence;
            }
          }
          if (r < 0.3f) r = 0.3f;
          if (r > 2.0f) r = 2.0f;

          // 3D point on deformed sphere
          float x3 = r * cosf(lat) * cosf(lonAngle);
          float y3 = r * sinf(lat);
          float z3 = r * cosf(lat) * sinf(lonAngle);

          // Rotate around Y
          float rx = x3 * cosR + z3 * sinR;
          float rz = -x3 * sinR + z3 * cosR;
          // Tilt around X
          float ry = y3 * tiltCos - rz * tiltSin;

          // Project
          int ppx = cx + (int)(rx * radius * 0.9f);
          int ppy = cy + (int)(ry * radius * 0.9f);

          if (prevPx >= 0)
          {
            int gray = 3 + (int)((r - 1.0f) * 4.0f);
            if (gray < 2) gray = 2;
            if (gray > 8) gray = 8;
            safeLine(fb, gray, prevPx, prevPy, ppx, ppy);
          }
          prevPx = ppx;
          prevPy = ppy;
        }
      }

      // Draw latitude lines
      for (int lat = 1; lat < kLatLines; lat++)
      {
        float latAngle = -1.5708f + (float)lat * 3.14159f / (float)kLatLines;
        int prevPx = -1, prevPy = -1;
        for (int p = 0; p <= kPointsPerLine; p++)
        {
          float lonAngle = (float)p * 6.28318f / (float)kPointsPerLine;
          float r = 1.0f;

          float sx = cosf(latAngle) * cosf(lonAngle);
          float sy = sinf(latAngle);
          float sz = cosf(latAngle) * sinf(lonAngle);
          for (int i = 0; i < nSections; i++)
          {
            float pa = poleAng[i];
            float px3 = cosf(pa);
            float pz3 = sinf(pa);
            float dist = (sx - px3) * (sx - px3) + sy * sy + (sz - pz3) * (sz - pz3);
            r += poleRad[i] * 0.3f / (dist + 0.1f);
            if (zeroRad[i] > 0.01f)
            {
              float zx3 = cosf(zeroAng[i]);
              float zz3 = sinf(zeroAng[i]);
              float zdist = (sx - zx3) * (sx - zx3) + sy * sy + (sz - zz3) * (sz - zz3);
              r -= zeroRad[i] * 0.2f / (zdist + 0.1f);
            }
          }
          if (r < 0.3f) r = 0.3f;
          if (r > 2.0f) r = 2.0f;

          float x3 = r * cosf(latAngle) * cosf(lonAngle);
          float y3 = r * sinf(latAngle);
          float z3 = r * cosf(latAngle) * sinf(lonAngle);

          float rx = x3 * cosR + z3 * sinR;
          float rz = -x3 * sinR + z3 * cosR;
          float ry = y3 * tiltCos - rz * tiltSin;

          int ppx = cx + (int)(rx * radius * 0.9f);
          int ppy = cy + (int)(ry * radius * 0.9f);

          if (prevPx >= 0)
          {
            int gray = 3 + (int)((r - 1.0f) * 4.0f);
            if (gray < 2) gray = 2;
            if (gray > 8) gray = 8;
            safeLine(fb, gray, prevPx, prevPy, ppx, ppy);
          }
          prevPx = ppx;
          prevPy = ppy;
        }
      }

      // Draw pole/zero gradient blobs
      for (int i = 0; i < nSections; i++)
      {
        // Pole blob (bright)
        float pa = poleAng[i];
        float px3 = cosf(pa) * cosR + sinf(pa) * sinR;
        float pz3 = -cosf(pa) * sinR + sinf(pa) * cosR;
        float py3 = -pz3 * tiltSin;
        if (-cosf(pa) * sinR + sinf(pa) * cosR > -0.3f) // front-facing check
        {
          int bx = cx + (int)(px3 * radius * 0.9f);
          int by = cy + (int)(py3 * radius * 0.9f);
          int bright = 8 + (int)(poleRad[i] * 7.0f);
          if (bright > 15) bright = 15;
          for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
            {
              int d = dx * dx + dy * dy;
              if (d > 5) continue;
              int g = bright - d * 2;
              if (g > 1)
                safePixel(fb, g, bx + dx, by + dy);
            }
        }

        // Zero blob (dim)
        if (zeroRad[i] > 0.01f)
        {
          float za = zeroAng[i];
          float zx3 = cosf(za) * cosR + sinf(za) * sinR;
          float zz3 = -cosf(za) * sinR + sinf(za) * cosR;
          float zy3 = -zz3 * tiltSin;
          if (zz3 > -0.3f)
          {
            int bx = cx + (int)(zx3 * radius * 0.9f);
            int by = cy + (int)(zy3 * radius * 0.9f);
            for (int dy = -1; dy <= 1; dy++)
              for (int dx = -1; dx <= 1; dx++)
              {
                int d = dx * dx + dy * dy;
                if (d > 2) continue;
                safePixel(fb, 4 - d, bx + dx, by + dy);
              }
          }
        }
      }
    }
  };

} // namespace stolmine
