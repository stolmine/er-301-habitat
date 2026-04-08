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

      // --- Compute target pole/zero positions (interior, r=0.5) ---
      // No tilt on attractor positions -- keeps blob centered in sphere
      float vizSlew = 0.12f;
      for (int i = 0; i < nSections; i++)
      {
        float pa = mpSfera->getPoleAngle(i);
        float pr = mpSfera->getPoleRadius(i);
        float ir = 0.5f;
        float x3 = ir * cosf(pa), z3 = ir * sinf(pa);
        // Rotate only (no tilt) so blob stays centered vertically
        float rx = x3 * cosR + z3 * sinR;
        float ry = 0.0f; // keep on equator for cleaner look
        float depth = -x3 * sinR + z3 * cosR;
        float df = 0.5f + 0.5f * depth; // 0=back, 1=front

        mPoleSxSlew[i] += (rx - mPoleSxSlew[i]) * vizSlew;
        mPoleSySlew[i] += (ry - mPoleSySlew[i]) * vizSlew;
        mPoleStrSlew[i] += (pr * (0.3f + 0.7f * df) - mPoleStrSlew[i]) * vizSlew;

        float za = mpSfera->getZeroAngle(i);
        float zr = mpSfera->getZeroRadius(i);
        if (zr > 0.01f)
        {
          float zx3 = ir * cosf(za), zz3 = ir * sinf(za);
          float zrx = zx3 * cosR + zz3 * sinR;
          mZeroSxSlew[i] += (zrx - mZeroSxSlew[i]) * vizSlew;
          mZeroSySlew[i] += (0.0f - mZeroSySlew[i]) * vizSlew;
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

      // --- Metaball field (Wyvill function, compact support) ---
      float invRx = 1.0f / (radX > 1 ? radX : 1);
      float invRy = 1.0f / (radY > 1 ? radY : 1);

      // Wyvill influence radius per blob
      float blobR = 0.6f;
      float blobR2 = blobR * blobR;
      float invBlobR2 = 1.0f / blobR2;

      for (int py = 0; py < h; py++)
      {
        float ny = ((float)py - (float)h * 0.5f) * invRy;

        for (int px = 0; px < w; px++)
        {
          float nx = ((float)px - (float)w * 0.5f) * invRx;

          // Hard clip to sphere boundary
          float sphereDist2 = nx * nx + ny * ny;
          if (sphereDist2 > 1.0f) continue;

          // Sphere shell: faint ring at edge
          float shell = 0.0f;
          if (sphereDist2 > 0.85f)
          {
            float t = (sphereDist2 - 0.85f) / 0.15f;
            shell = t * 3.0f;
          }

          // Metaball evaluation point (with parallax offset from shell)
          float mx = nx - parallaxX;
          float my = ny - parallaxY;

          // Wyvill field: sum (1 - d²/R²)³ for each pole
          float field = 0.0f;
          for (int i = 0; i < 7; i++)
          {
            if (mPoleStrSlew[i] < 0.005f) continue;
            float dx = mx - mPoleSxSlew[i];
            float dy = my - mPoleSySlew[i];
            float d2 = dx * dx + dy * dy;
            if (d2 >= blobR2) continue; // compact support: skip if outside R
            float t = 1.0f - d2 * invBlobR2;
            float w3 = t * t * t; // (1 - d²/R²)³
            field += mPoleStrSlew[i] * w3;
          }

          // Subtract zero contributions (smaller influence radius)
          float zeroR2 = 0.2f;
          float invZeroR2 = 1.0f / zeroR2;
          for (int i = 0; i < 7; i++)
          {
            if (mZeroStrSlew[i] < 0.005f) continue;
            float dx = mx - mZeroSxSlew[i];
            float dy = my - mZeroSySlew[i];
            float d2 = dx * dx + dy * dy;
            if (d2 >= zeroR2) continue;
            float t = 1.0f - d2 * invZeroR2;
            field -= mZeroStrSlew[i] * 0.3f * t * t * t;
          }

          // Containment: smooth falloff at sphere boundary
          float contain = 1.0f;
          if (sphereDist2 > 0.7f)
          {
            float t = (1.0f - sphereDist2) / 0.3f; // 1 at r=0.7, 0 at r=1.0
            contain = t * t; // smooth rolloff
          }
          field *= contain;

          // Threshold + brightness
          float blob = 0.0f;
          if (field > 0.15f)
          {
            blob = (field - 0.15f) * 14.0f;
            if (blob > 11.0f) blob = 11.0f;
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
