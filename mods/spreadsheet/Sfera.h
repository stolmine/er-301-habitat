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
    float getParamX();
    float getParamY();
    float getEnvelope();
    float getSpin();

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
    od::Parameter mSpin{"Spin", 0.0f};

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
    float mSpinVelocity = 0.0f;
    float mEnvSlew = 0.0f;

    // Slewed pole/zero screen positions for smooth visual transitions
    float mPoleSxSlew[7], mPoleSySlew[7], mPoleStrSlew[7];
    float mZeroSxSlew[7], mZeroSySlew[7], mZeroStrSlew[7];
    bool mSlewInit = false;
    uint32_t mNoiseState = 12345;
    int mFrameCount = 0;

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
      float radX = (float)w * 0.42f;
      float radY = (float)h * 0.42f;

      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + w - 1, mWorldBottom + h - 1);

      if (!mpSfera) return;

      mFrameCount++;

      // --- Audio envelope (slewed for smooth viz) ---
      float rawEnv = mpSfera->getEnvelope();
      float envTarget = rawEnv / (rawEnv + 0.12f);
      mEnvSlew += (envTarget - mEnvSlew) * 0.18f;
      float env = mEnvSlew;

      // --- Spin: user param + audio energy ---
      float spinParam = mpSfera->getSpin();
      mTargetAngle = mpSfera->getSphereRotation();
      float diff = mTargetAngle - mCurrentAngle;
      while (diff > 3.14159f) diff -= 6.28318f;
      while (diff < -3.14159f) diff += 6.28318f;
      // Spin param drives constant rotation; audio adds on top
      float audioSpin = env * 0.08f;
      mSpinVelocity += (spinParam * 0.15f + audioSpin - mSpinVelocity) * 0.08f;
      mCurrentAngle += diff * 0.06f + mSpinVelocity;

      float cosR = cosf(mCurrentAngle);
      float sinR = sinf(mCurrentAngle);

      int nSections = mpSfera->getActiveSections();
      if (nSections > 7) nSections = 7;

      if (!mSlewInit)
      {
        for (int i = 0; i < 7; i++)
        {
          mPoleSxSlew[i] = mPoleSySlew[i] = mPoleStrSlew[i] = 0;
          mZeroSxSlew[i] = mZeroSySlew[i] = mZeroStrSlew[i] = 0;
        }
        mSlewInit = true;
      }

      float paramX = mpSfera->getParamX();
      float paramY = mpSfera->getParamY();

      auto vizNoise = [&]() -> float {
        mNoiseState = mNoiseState * 1103515245u + 12345u;
        return ((float)((mNoiseState >> 8) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
      };

      // --- Compute pole/zero positions ---
      float vizSlew = 0.12f;
      float goldenAngle = 2.399963f;
      // Poles reach much further at high energy -- full separation possible
      float envReach = 0.12f + env * 0.55f;
      float envJitter = env * 0.08f;
      float absSpin = spinParam < 0 ? -spinParam : spinParam;

      for (int i = 0; i < nSections; i++)
      {
        float pa = mpSfera->getPoleAngle(i);
        float pr = mpSfera->getPoleRadius(i);

        float spiralAngle = (float)i * goldenAngle + mCurrentAngle + pa * 0.5f;
        spiralAngle += paramX * 1.5f;
        // Spin param also spreads poles apart
        float spiralR = envReach + pr * 0.25f + paramY * 0.06f + absSpin * 0.12f;
        float noiseX = vizNoise() * (0.008f + envJitter);
        float noiseY = vizNoise() * (0.008f + envJitter);
        float tx = spiralR * cosf(spiralAngle) + noiseX;
        float ty = spiralR * sinf(spiralAngle) + noiseY;

        float paramBoost = (i & 1) ? (0.6f + paramY * 0.5f) : (0.6f + paramX * 0.5f);
        float envBoost = 1.0f + env * 2.0f;

        mPoleSxSlew[i] += (tx - mPoleSxSlew[i]) * vizSlew;
        mPoleSySlew[i] += (ty - mPoleSySlew[i]) * vizSlew;
        mPoleStrSlew[i] += (pr * paramBoost * envBoost - mPoleStrSlew[i]) * vizSlew;

        float zr = mpSfera->getZeroRadius(i);
        if (zr > 0.01f)
        {
          float zAngle = spiralAngle + 3.14159f;
          float zSpiralR = 0.12f + zr * 0.18f + env * 0.12f;
          mZeroSxSlew[i] += (zSpiralR * cosf(zAngle) - mZeroSxSlew[i]) * vizSlew;
          mZeroSySlew[i] += (zSpiralR * sinf(zAngle) - mZeroSySlew[i]) * vizSlew;
          mZeroStrSlew[i] += (zr * 0.6f * envBoost - mZeroStrSlew[i]) * vizSlew;
        }
        else
          mZeroStrSlew[i] *= (1.0f - vizSlew);
      }
      for (int i = nSections; i < 7; i++)
      {
        mPoleStrSlew[i] *= (1.0f - vizSlew);
        mZeroStrSlew[i] *= (1.0f - vizSlew);
      }

      // --- Parallax ---
      float parallaxX = sinR * (0.04f + env * 0.10f);
      float parallaxY = -cosR * (0.02f + env * 0.06f);

      // --- Directional light for 3D depth ---
      // Light from upper-left: normal dot lightDir gives diffuse
      float lightX = -0.4f, lightY = 0.5f, lightZ = 0.75f;
      // Normalize
      float lightLen = 1.0f / (0.001f + sqrtf(lightX*lightX + lightY*lightY + lightZ*lightZ));
      lightX *= lightLen; lightY *= lightLen; lightZ *= lightLen;

      // --- Field parameters ---
      float invRx = 1.0f / (radX > 1 ? radX : 1);
      float invRy = 1.0f / (radY > 1 ? radY : 1);

      // Body shrinks with energy -- separation from poles
      float bodyR2 = 0.16f + env * 0.12f;
      float invBodyR2 = 1.0f / bodyR2;

      // Arm reach grows dramatically
      float armR2 = 0.12f + env * 0.30f + absSpin * 0.08f;
      float invArmR2 = 1.0f / armR2;

      float zeroR2 = 0.07f + env * 0.06f;
      float invZeroR2 = 1.0f / zeroR2;

      // Body weakens as energy rises -- lets poles detach
      float totalStr = 0.0f;
      for (int i = 0; i < 7; i++) totalStr += mPoleStrSlew[i];
      float bodyStr = 0.6f + totalStr * 0.15f - env * 0.2f;
      if (bodyStr < 0.25f) bodyStr = 0.25f;
      if (bodyStr > 1.5f) bodyStr = 1.5f;

      // Low threshold = thin tendrils visible
      float threshold = 0.15f - env * 0.12f;
      if (threshold < 0.02f) threshold = 0.02f;

      // Containment relaxes -- blobs can push to edge
      float containStart = 0.65f - env * 0.20f - absSpin * 0.10f;
      if (containStart < 0.35f) containStart = 0.35f;

      for (int py = 0; py < h; py++)
      {
        float ny = ((float)py - (float)h * 0.5f) * invRy;

        for (int px = 0; px < w; px++)
        {
          float nx = ((float)px - (float)w * 0.5f) * invRx;

          float sphereDist2 = nx * nx + ny * ny;
          if (sphereDist2 > 1.0f) continue;

          // --- 3D surface normal for lighting ---
          // Approximate z from sphere: z = sqrt(1 - x^2 - y^2)
          float zSurf = 1.0f - sphereDist2; // skip sqrt, use z^2 proxy
          float zApprox = zSurf * (1.0f + 0.25f * zSurf); // cheap sqrt approx

          // Diffuse: dot(normal, light). Normal ~ (nx, ny, zApprox)
          float diffuse = nx * lightX + ny * lightY + zApprox * lightZ;
          if (diffuse < 0.0f) diffuse = 0.0f;

          // Specular: reflection highlight for glossy ferrofluid look
          // Half-vector approximation: boost where normal aligns with light
          float spec = diffuse * diffuse * diffuse * diffuse; // pow4 falloff
          float lighting = diffuse * 0.7f + spec * 0.5f;
          // Ambient floor -- edges go nearly black
          lighting += 0.08f;
          if (lighting > 1.0f) lighting = 1.0f;

          // Shell outline
          float shell = 0.0f;
          float shellEdge = 0.92f - env * 0.04f;
          if (sphereDist2 > shellEdge)
          {
            float t = (sphereDist2 - shellEdge) / (1.0f - shellEdge);
            shell = t * 1.5f * lighting;
          }

          float mx = nx - parallaxX;
          float my = ny - parallaxY;

          // Central body
          float bodyD2 = mx * mx + my * my;
          float field = 0.0f;
          if (bodyD2 < bodyR2)
          {
            float t = 1.0f - bodyD2 * invBodyR2;
            field += bodyStr * t * t * t;
          }

          // Pole protrusions -- stronger, reach further
          for (int i = 0; i < 7; i++)
          {
            if (mPoleStrSlew[i] < 0.005f) continue;
            float dx = mx - mPoleSxSlew[i];
            float dy = my - mPoleSySlew[i];
            float d2 = dx * dx + dy * dy;
            if (d2 >= armR2) continue;
            float t = 1.0f - d2 * invArmR2;
            field += mPoleStrSlew[i] * 2.5f * t * t * t;
          }

          // Zero dimples
          for (int i = 0; i < 7; i++)
          {
            if (mZeroStrSlew[i] < 0.005f) continue;
            float dx = mx - mZeroSxSlew[i];
            float dy = my - mZeroSySlew[i];
            float d2 = dx * dx + dy * dy;
            if (d2 >= zeroR2) continue;
            float t = 1.0f - d2 * invZeroR2;
            field -= mZeroStrSlew[i] * 0.7f * t * t * t;
          }

          // Containment
          if (sphereDist2 > containStart)
          {
            float t = (1.0f - sphereDist2) / (1.0f - containStart);
            if (t < 0.0f) t = 0.0f;
            field *= t * t;
          }

          // Threshold + lighting
          float blob = 0.0f;
          if (field > threshold)
          {
            blob = (field - threshold) * 15.0f * lighting;
            if (blob > 13.0f) blob = 13.0f;
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
