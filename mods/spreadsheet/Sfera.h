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

    // Slewed pole/zero screen positions for smooth visual transitions
    float mPoleSxSlew[7], mPoleSySlew[7], mPoleStrSlew[7];
    float mZeroSxSlew[7], mZeroSySlew[7], mZeroStrSlew[7];
    bool mSlewInit = false;

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

      // --- Init slew state ---
      if (!mSlewInit)
      {
        for (int i = 0; i < 7; i++)
        {
          mPoleSxSlew[i] = mPoleSySlew[i] = mPoleStrSlew[i] = 0;
          mZeroSxSlew[i] = mZeroSySlew[i] = mZeroStrSlew[i] = 0;
        }
        mSlewInit = true;
      }

      // --- Compute target pole/zero positions ---
      // Spread poles in 2D using golden angle spiral for even distribution
      // This creates distinct attractor points that pull the ferrofluid blob
      // into separate lobes/protrusions
      float vizSlew = 0.10f;
      float goldenAngle = 2.399963f; // pi * (3 - sqrt(5))
      for (int i = 0; i < nSections; i++)
      {
        float pa = mpSfera->getPoleAngle(i);
        float pr = mpSfera->getPoleRadius(i);

        // Golden spiral placement: each pole gets a unique (x,y) inside the sphere
        // pa controls the radial distance from center, index controls angular position
        float spiralAngle = (float)i * goldenAngle + mCurrentAngle;
        float spiralR = 0.15f + pr * 0.35f; // 0.15 to 0.50 from center
        // Modulate by pole angle to create movement with filter changes
        spiralAngle += pa * 0.5f;
        float tx = spiralR * cosf(spiralAngle);
        float ty = spiralR * sinf(spiralAngle);

        // Depth fade: poles with higher angle (higher freq) are "deeper"
        float df = 0.5f + 0.5f * cosf(pa * 0.5f);

        mPoleSxSlew[i] += (tx - mPoleSxSlew[i]) * vizSlew;
        mPoleSySlew[i] += (ty - mPoleSySlew[i]) * vizSlew;
        mPoleStrSlew[i] += (pr * (0.4f + 0.6f * df) - mPoleStrSlew[i]) * vizSlew;

        float za = mpSfera->getZeroAngle(i);
        float zr = mpSfera->getZeroRadius(i);
        if (zr > 0.01f)
        {
          // Zeros placed opposite to corresponding pole
          float zAngle = spiralAngle + 3.14159f;
          float zSpiralR = 0.2f + zr * 0.2f;
          mZeroSxSlew[i] += (zSpiralR * cosf(zAngle) - mZeroSxSlew[i]) * vizSlew;
          mZeroSySlew[i] += (zSpiralR * sinf(zAngle) - mZeroSySlew[i]) * vizSlew;
          mZeroStrSlew[i] += (zr - mZeroStrSlew[i]) * vizSlew;
        }
        else
          mZeroStrSlew[i] *= (1.0f - vizSlew);
      }
      for (int i = nSections; i < 7; i++)
      {
        mPoleStrSlew[i] *= (1.0f - vizSlew);
        mZeroStrSlew[i] *= (1.0f - vizSlew);
      }

      // --- Parallax: offset metaball evaluation origin relative to shell ---
      // Shell is fixed at screen center. Blob origin shifts with rotation.
      float parallaxX = sinR * 0.06f;  // small horizontal shift
      float parallaxY = -cosR * 0.03f; // tiny vertical shift

      // --- Wyvill ferrofluid field ---
      // Base blob at center provides cohesive body.
      // Individual poles pull protrusions outward from the body.
      // Compact support keeps everything contained.

      float invRx = 1.0f / (radX > 1 ? radX : 1);
      float invRy = 1.0f / (radY > 1 ? radY : 1);

      // Base body: large Wyvill blob at center
      float bodyR2 = 0.25f; // R=0.5, body fills inner half of sphere
      float invBodyR2 = 1.0f / bodyR2;

      // Pole protrusion influence radius (each pole's arm)
      float armR2 = 0.20f; // R~0.45
      float invArmR2 = 1.0f / armR2;

      // Zero dimple radius
      float zeroR2 = 0.09f;
      float invZeroR2 = 1.0f / zeroR2;

      // Sum total pole strength for body size scaling
      float totalStr = 0.0f;
      for (int i = 0; i < 7; i++) totalStr += mPoleStrSlew[i];
      float bodyStr = 0.4f + totalStr * 0.3f; // body grows with more active poles
      if (bodyStr > 1.2f) bodyStr = 1.2f;

      for (int py = 0; py < h; py++)
      {
        float ny = ((float)py - (float)h * 0.5f) * invRy;

        for (int px = 0; px < w; px++)
        {
          float nx = ((float)px - (float)w * 0.5f) * invRx;

          // Hard clip to sphere
          float sphereDist2 = nx * nx + ny * ny;
          if (sphereDist2 > 1.0f) continue;

          // Shell outline
          float shell = 0.0f;
          if (sphereDist2 > 0.88f)
          {
            float t = (sphereDist2 - 0.88f) / 0.12f;
            shell = t * 2.5f;
          }

          // Parallax offset for metaball evaluation
          float mx = nx - parallaxX;
          float my = ny - parallaxY;

          // Central body blob
          float bodyD2 = mx * mx + my * my;
          float field = 0.0f;
          if (bodyD2 < bodyR2)
          {
            float t = 1.0f - bodyD2 * invBodyR2;
            field += bodyStr * t * t * t;
          }

          // Pole protrusions: arms stretching outward from body
          for (int i = 0; i < 7; i++)
          {
            if (mPoleStrSlew[i] < 0.005f) continue;
            float dx = mx - mPoleSxSlew[i];
            float dy = my - mPoleSySlew[i];
            float d2 = dx * dx + dy * dy;
            if (d2 >= armR2) continue;
            float t = 1.0f - d2 * invArmR2;
            field += mPoleStrSlew[i] * 1.5f * t * t * t;
          }

          // Zero dimples: subtract from field
          for (int i = 0; i < 7; i++)
          {
            if (mZeroStrSlew[i] < 0.005f) continue;
            float dx = mx - mZeroSxSlew[i];
            float dy = my - mZeroSySlew[i];
            float d2 = dx * dx + dy * dy;
            if (d2 >= zeroR2) continue;
            float t = 1.0f - d2 * invZeroR2;
            field -= mZeroStrSlew[i] * 0.5f * t * t * t;
          }

          // Containment falloff at sphere edge
          if (sphereDist2 > 0.65f)
          {
            float t = (1.0f - sphereDist2) / 0.35f;
            if (t < 0.0f) t = 0.0f;
            field *= t * t;
          }

          // Threshold and shade
          float blob = 0.0f;
          if (field > 0.15f)
          {
            blob = (field - 0.15f) * 12.0f;
            if (blob > 12.0f) blob = 12.0f;
          }

          int gray = (int)(blob + shell);
          if (gray > 13) gray = 13;
          if (gray < 1) continue;

          safePixel(fb, gray, mWorldLeft + px, mWorldBottom + py);
        }
      }
    }
  };

} // namespace stolmine
