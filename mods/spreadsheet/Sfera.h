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

      // --- Init slew state on first frame ---
      if (!mSlewInit)
      {
        for (int i = 0; i < 7; i++)
        {
          mPoleSxSlew[i] = mPoleSySlew[i] = mPoleStrSlew[i] = 0;
          mZeroSxSlew[i] = mZeroSySlew[i] = mZeroStrSlew[i] = 0;
        }
        mSlewInit = true;
      }

      // --- Compute target pole/zero positions (interior, r=0.55) ---
      float vizSlew = 0.12f; // visual transition speed
      for (int i = 0; i < nSections; i++)
      {
        float pa = mpSfera->getPoleAngle(i);
        float pr = mpSfera->getPoleRadius(i);
        // Place attractors inside sphere at r=0.55 (not on surface)
        float ir = 0.55f;
        float x3 = ir * cosf(pa), z3 = ir * sinf(pa);
        float rx = x3 * cosR + z3 * sinR;
        float rz = -x3 * sinR + z3 * cosR;
        float ry = -rz * tiltSin;
        float df = 0.4f + 0.6f * (0.5f + 0.5f * rz);

        mPoleSxSlew[i] += (rx - mPoleSxSlew[i]) * vizSlew;
        mPoleSySlew[i] += (ry - mPoleSySlew[i]) * vizSlew;
        mPoleStrSlew[i] += (pr * df - mPoleStrSlew[i]) * vizSlew;

        float za = mpSfera->getZeroAngle(i);
        float zr = mpSfera->getZeroRadius(i);
        if (zr > 0.01f)
        {
          float zx3 = ir * cosf(za), zz3 = ir * sinf(za);
          float zrx = zx3 * cosR + zz3 * sinR;
          float zrz = -zx3 * sinR + zz3 * cosR;
          mZeroSxSlew[i] += (zrx - mZeroSxSlew[i]) * vizSlew;
          mZeroSySlew[i] += (-zrz * tiltSin - mZeroSySlew[i]) * vizSlew;
          mZeroStrSlew[i] += (zr - mZeroStrSlew[i]) * vizSlew;
        }
        else
        {
          mZeroStrSlew[i] *= (1.0f - vizSlew); // fade out
        }
      }
      // Fade out unused sections
      for (int i = nSections; i < 7; i++)
      {
        mPoleStrSlew[i] *= (1.0f - vizSlew);
        mZeroStrSlew[i] *= (1.0f - vizSlew);
      }

      // --- Metaball field rendering (strictly bounded to sphere) ---
      float invRx = 1.0f / (radX > 1 ? radX : 1);
      float invRy = 1.0f / (radY > 1 ? radY : 1);

      for (int py = 0; py < h; py++)
      {
        float ny = ((float)py - (float)h * 0.5f) * invRy;

        for (int px = 0; px < w; px++)
        {
          float nx = ((float)px - (float)w * 0.5f) * invRx;

          // Hard clip to sphere boundary
          float sphereDist2 = nx * nx + ny * ny;
          if (sphereDist2 > 1.0f) continue;

          // Sphere shell outline
          float shell = 0.0f;
          if (sphereDist2 > 0.88f)
            shell = (sphereDist2 - 0.88f) / 0.12f * 2.5f;

          // Metaball field from slewed pole positions
          float field = 0.0f;
          for (int i = 0; i < 7; i++)
          {
            if (mPoleStrSlew[i] < 0.005f) continue;
            float dx = nx - mPoleSxSlew[i];
            float dy = ny - mPoleSySlew[i];
            float d2 = dx * dx + dy * dy + 0.02f;
            field += mPoleStrSlew[i] * 0.12f / d2;
          }

          // Subtract zero contributions
          for (int i = 0; i < 7; i++)
          {
            if (mZeroStrSlew[i] < 0.005f) continue;
            float dx = nx - mZeroSxSlew[i];
            float dy = ny - mZeroSySlew[i];
            float d2 = dx * dx + dy * dy + 0.03f;
            field -= mZeroStrSlew[i] * 0.04f / d2;
          }

          // Combine shell + metaball
          float blob = 0.0f;
          if (field > 0.25f)
            blob = (field - 0.25f) * 10.0f;
          if (blob > 11.0f) blob = 11.0f;

          int gray = (int)(blob + shell);
          if (gray > 13) gray = 13;
          if (gray < 1) continue;

          safePixel(fb, gray, mWorldLeft + px, mWorldBottom + py);
        }
      }
    }
  };

} // namespace stolmine
