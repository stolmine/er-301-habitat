// scope_unit::ScopeGraphic
//
// Package-side custom scope graphic. Aped from od::MiniScope so the
// scope package can ship user-controllable timebase + Y-gain without
// modifying the firmware.
//
// All virtuals are defined inline per docs/graphics-authoring-guide.md.
// No .cpp file — keeps vtable + typeinfo COMDAT (weak) so symbol
// resolution matches the firmware's vague-linkage base-class vtables
// across the package/firmware boundary on Cortex-A8.

#pragma once

#include <od/AudioThread.h>
#include <od/config.h>
#include <od/extras/FastEWMA.h>
#include <od/graphics/FrameBuffer.h>
#include <od/graphics/Graphic.h>
#include <od/objects/Inlet.h>
#include <od/objects/Outlet.h>
#include <od/objects/measurement/FifoProbe.h>
#include <vector>

namespace scope_unit
{

  class ScopeGraphic : public od::Graphic
  {
  public:
    ScopeGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height)
    {
      mMaximums.resize(mWidth + 1, 0);
      mMinimums.resize(mWidth + 1, 0);
      mEWMA.setInitialState(0.0f);
      mEWMA.setTimeConstant(globalConfig.sampleRate * 0.25f, 1.0f);
      mShowStatus = mWidth > 50;
    }

    virtual ~ScopeGraphic()
    {
      disconnectProbe();
      clearOutlet();
    }

    void watchOutlet(od::Outlet *outlet)
    {
      disconnectProbe();
      clearOutlet();
      mpWatchedOutlet = outlet;
      if (mpWatchedOutlet)
      {
        mpWatchedOutlet->attach();
        if (mVisibility == od::visibleState)
          connectProbe();
      }
    }

    void setDecimation(int d)
    {
      if (d < 1) d = 1;
      mDecimation = d;
      if (mpProbe) mpProbe->setDecimation(d);
    }

    void setGain(float g)
    {
      mGain = g;
    }

    void setOffset(float offset)
    {
      mOffset = offset;
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      od::Graphic::draw(fb);

      int x0 = mWorldLeft;
      int x1 = x0 + mWidth - 1;
      int mid = mWorldBottom + mHeight / 2;

      // baseline + quarter rules
      fb.hline(GRAY3, x0, x1, mid);
      fb.hline(GRAY3, x0, x1, mWorldBottom + mHeight / 4, 2);
      fb.hline(GRAY3, x0, x1, mWorldBottom + (3 * mHeight) / 4, 2);

      if (mpProbe == 0)
      {
        if (mShowStatus)
        {
          fb.text(WHITE, mWorldLeft + mWidth / 2 - 5, mid + 4, "No", 10);
          fb.text(WHITE, mWorldLeft + mWidth / 2 - 10, mid - 10, "Signal", 10);
        }
        return;
      }

      if (mCalculateCount >= RefreshTime)
      {
        calculate();
        mCalculateCount = 0;
      }
      else if (mCalculateCount >= 0)
      {
        mCalculateCount++;
      }

      if (mCalculateCount < 0)
      {
        return;
      }

      od::Color color = fb.mIsMonoChrome ? WHITE : GRAY3;
      for (int i = 0; i < mWidth; i++)
      {
        int px = mWorldLeft + i;
        int py0 = mid + mMinimums[i];
        int py1 = mid + mMaximums[i];
        fb.vline(color, px, py0, py1);
        fb.pixel(mForeground, px, py0);
        fb.pixel(mForeground, px, py1);
      }
    }

    virtual void notifyHidden()
    {
      disconnectProbe();
      od::Graphic::notifyHidden();
    }

    virtual void notifyVisible()
    {
      connectProbe();
      od::Graphic::notifyVisible();
    }

  private:
    od::FifoProbe *mpProbe = nullptr;
    od::Outlet    *mpWatchedOutlet = nullptr;
    od::FastEWMA   mEWMA;
    std::vector<int> mMaximums;
    std::vector<int> mMinimums;
    float mTriggerThreshold = 0.0f;
    float mGain = 1.0f;
    float mOffset = 0.0f;
    int   mHorizontalSync = 0;
    int   mDecimation = 2;
    int   mCalculateCount = 0;
    bool  mShowStatus = false;

    static const int WarmUpTime = 10;
    static const int RefreshTime = 2;

    void calculate()
    {
      if (mpProbe == 0) return;

      int n = (int)mpProbe->size();
      if (n == 0) return;
      float *values = mpProbe->get(n);

      mTriggerThreshold = mEWMA.push(values, n);

      // search outward from the buffer midpoint for a below-to-above
      // (or above-to-below) crossing — gives a stable trigger.
      int n2 = n / 2;
      int n4 = n2 / 2;
      mHorizontalSync = 0;
      for (int i = 1; i < n4; i++)
      {
        if (values[n2 + i - 1] < mTriggerThreshold && values[n2 + i] > mTriggerThreshold)
        {
          mHorizontalSync = i;
          break;
        }
        if (values[n2 - (i - 1)] > mTriggerThreshold && values[n2 - i] < mTriggerThreshold)
        {
          mHorizontalSync = -i;
          break;
        }
      }

      int imin = n2 + mHorizontalSync - n4;
      int imax = imin + n2;

      for (int i = 0; i < mWidth; i++)
      {
        mMaximums[i] = -mHeight;
        mMinimums[i] = mHeight;
      }

      float dx = (float)mWidth / (float)n2;
      float dy = (float)(mHeight - 1) * 0.5f;
      for (int i = imin; i < imax; i++)
      {
        int x = (int)((i - imin) * dx);
        int y = (int)((mOffset + values[i] * mGain) * dy);
        if (mMaximums[x] < y) mMaximums[x] = y;
        if (mMinimums[x] > y) mMinimums[x] = y;
      }

      // close any pixel-column gaps caused by aliasing between
      // discrete sample indices and pixel buckets.
      for (int i = 1; i < mWidth; i++)
      {
        if (mMaximums[i] < mMinimums[i - 1]) mMaximums[i] = mMinimums[i - 1];
        if (mMinimums[i] > mMaximums[i - 1]) mMinimums[i] = mMaximums[i - 1];
      }
    }

    void connectProbe()
    {
      if (mpProbe) return;
      if (!mpWatchedOutlet) return;

      mpProbe = od::AudioThread::getFifoProbe();
      if (mpProbe)
      {
        mpProbe->setDecimation(mDecimation);
        od::AudioThread::connect(mpWatchedOutlet, &mpProbe->mInput);
      }

      mCalculateCount = -WarmUpTime;
    }

    void disconnectProbe()
    {
      if (mpProbe)
      {
        od::AudioThread::disconnect(&mpProbe->mInput);
        od::AudioThread::releaseFifoProbe(mpProbe);
        mpProbe = nullptr;
        mEWMA.setInitialState(0.0f);
      }
    }

    void clearOutlet()
    {
      if (mpWatchedOutlet)
      {
        mpWatchedOutlet->release();
        mpWatchedOutlet = nullptr;
      }
    }
#endif
  };

} // namespace scope_unit
