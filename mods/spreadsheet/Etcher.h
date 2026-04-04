#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>

namespace stolmine
{

  static const int kMaxSegments = 32;

  enum CurveType
  {
    CURVE_NONE = 0,
    CURVE_LINEAR,
    CURVE_CUBIC,
    CURVE_COUNT
  };

  enum DeviationScope
  {
    DEV_OFFSET = 0,
    DEV_CURVE,
    DEV_WEIGHT,
    DEV_ALL,
    DEV_COUNT
  };

  class Etcher : public od::Object
  {
  public:
    Etcher();
    virtual ~Etcher();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mInput{"Input"};
    od::Outlet mOut{"Out"};

    od::Parameter mSkew{"Skew", 1.0f};
    od::Parameter mLevel{"Level", 1.0f};
    od::Parameter mSegmentCount{"SegmentCount", 16.0f};
    od::Parameter mDeviation{"Deviation", 0.0f};
    od::Parameter mDeviationScope{"DeviationScope", 0.0f};

    // Edit buffer: scratch params for Readout binding
    od::Parameter mEditOffset{"EditOffset", 0.0f};
    od::Parameter mEditCurve{"EditCurve", 1.0f};
    od::Parameter mEditWeight{"EditWeight", 1.0f};
#endif

    // SWIG-visible: segment data accessors
    float getSegmentOffset(int i);
    void setSegmentOffset(int i, float v);
    int getSegmentCurve(int i);
    void setSegmentCurve(int i, int v);
    float getSegmentWeight(int i);
    void setSegmentWeight(int i, float v);

    void loadSegment(int i);
    void storeSegment(int i);

    // SWIG-visible: UI state
    int getActiveSegment() { return mActiveSegment; }
    float getCurrentInput() { return mCurrentInput; }
    float getCurrentOutput() { return mCurrentOutput; }
    int getSegmentCount();

    // For graphic: computed boundary positions (normalized 0-1)
    float getSegmentBoundary(int i);

    // Evaluate transfer function at normalized input (0-1), for curve drawing
    float evaluate(float normalizedInput);

#ifndef SWIGLUA
  private:
    struct Internal;
    Internal *mpInternal;

    int mActiveSegment = 0;
    int mCachedSegmentCount = 16;
    float mCurrentInput = 0.0f;
    float mCurrentOutput = 0.0f;

    float mBoundaries[kMaxSegments + 1];
    bool mBoundariesDirty = true;
    float mLastSkew = 1.0f;
    int mLastSegCount = 16;

    void recomputeBoundaries();
    void checkBoundariesDirty();
    float interpolateSegment(int seg, float frac);

    // Deviation: snapshot on segment transition
    int mLastActiveSegment = -1;
    float mDevOffsetSnap = 0.0f;
    int mDevCurveSnap = -1;    // -1 = no override
    float mDevWeightSnap = 0.0f;

    void rollDeviation(int seg);
#endif
  };

} // namespace stolmine
