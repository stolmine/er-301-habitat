// Etcher -- CV-addressed piecewise transfer function for ER-301

#include "Etcher.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

namespace stolmine
{

  // Simple LCG random for deviation
  static uint32_t sRandState = 54321;
  static inline float randFloat()
  {
    sRandState = sRandState * 1664525u + 1013904223u;
    return (float)((int32_t)sRandState) / (float)0x7FFFFFFF;
  }

  struct Etcher::Internal
  {
    float offset[kMaxSegments];
    int curve[kMaxSegments];
    float weight[kMaxSegments];

    // Transform work buffers (heap, not stack)
    float tmpOffset[kMaxSegments];
    int tmpCurve[kMaxSegments];
    float tmpWeight[kMaxSegments];

    void Init()
    {
      for (int i = 0; i < kMaxSegments; i++)
      {
        offset[i] = 0.0f;
        curve[i] = CURVE_LINEAR;
        weight[i] = 1.0f;
      }
    }
  };

  Etcher::Etcher()
  {
    addInput(mInput);
    addOutput(mOut);
    addParameter(mSkew);
    addParameter(mLevel);
    addParameter(mSegmentCount);
    addParameter(mDeviation);
    addParameter(mDeviationScope);
    addParameter(mEditOffset);
    addParameter(mEditCurve);
    addParameter(mEditWeight);
    addInput(mTransform);
    addParameter(mTransformFunc);
    addParameter(mTransformDepth);

    mpInternal = new Internal();
    mpInternal->Init();

    mTransformWasHigh = false;
    mManualFire = false;
    mActiveSegment = 0;
    mCachedSegmentCount = 16;
    mCurrentInput = 0.0f;
    mCurrentOutput = 0.0f;
    mBoundariesDirty = true;
    mLastSkew = 0.0f;
    mLastSegCount = 16;
    mLastActiveSegment = -1;
    mDevOffsetSnap = 0.0f;
    mDevCurveSnap = -1;
    mDevWeightSnap = 0.0f;

    recomputeBoundaries();
  }

  Etcher::~Etcher()
  {
    delete mpInternal;
  }

  // --- Segment data accessors ---

  float Etcher::getSegmentOffset(int i)
  {
    return mpInternal->offset[CLAMP(0, kMaxSegments - 1, i)];
  }

  void Etcher::setSegmentOffset(int i, float v)
  {
    mpInternal->offset[CLAMP(0, kMaxSegments - 1, i)] = CLAMP(-1.0f, 1.0f, v);
  }

  int Etcher::getSegmentCurve(int i)
  {
    return mpInternal->curve[CLAMP(0, kMaxSegments - 1, i)];
  }

  void Etcher::setSegmentCurve(int i, int v)
  {
    mpInternal->curve[CLAMP(0, kMaxSegments - 1, i)] = CLAMP(0, (int)CURVE_COUNT - 1, v);
  }

  float Etcher::getSegmentWeight(int i)
  {
    return mpInternal->weight[CLAMP(0, kMaxSegments - 1, i)];
  }

  void Etcher::setSegmentWeight(int i, float v)
  {
    mpInternal->weight[CLAMP(0, kMaxSegments - 1, i)] = CLAMP(0.1f, 4.0f, v);
    mBoundariesDirty = true;
  }

  void Etcher::loadSegment(int i)
  {
    i = CLAMP(0, kMaxSegments - 1, i);
    mEditOffset.hardSet(mpInternal->offset[i]);
    mEditCurve.hardSet((float)mpInternal->curve[i]);
    mEditWeight.hardSet(mpInternal->weight[i]);
  }

  void Etcher::storeSegment(int i)
  {
    i = CLAMP(0, kMaxSegments - 1, i);
    mpInternal->offset[i] = CLAMP(-1.0f, 1.0f, mEditOffset.value());
    mpInternal->curve[i] = CLAMP(0, (int)CURVE_COUNT - 1, (int)(mEditCurve.value() + 0.5f));
    float newWeight = CLAMP(0.1f, 4.0f, mEditWeight.value());
    if (mpInternal->weight[i] != newWeight)
    {
      mpInternal->weight[i] = newWeight;
      mBoundariesDirty = true;
    }
  }

  // --- Boundary computation ---

  void Etcher::recomputeBoundaries()
  {
    Internal &s = *mpInternal;
    int segCount = mCachedSegmentCount;
    float skew = CLAMP(-1.0f, 1.0f, mSkew.value());

    // Sum active weights
    float totalWeight = 0.0f;
    for (int i = 0; i < segCount; i++)
      totalWeight += s.weight[i];
    if (totalWeight < 0.001f)
      totalWeight = 1.0f;

    // Accumulate normalized boundaries, then apply symmetric skew
    mBoundaries[0] = 0.0f;
    float accum = 0.0f;
    for (int i = 0; i < segCount; i++)
    {
      accum += s.weight[i] / totalWeight;
      float raw = accum;
      // Symmetric skew: shift proportional to distance from nearer edge
      // Positive skew bunches boundaries low, negative bunches high
      float margin = (raw < 1.0f - raw) ? raw : 1.0f - raw;
      float shifted = raw - skew * margin;
      mBoundaries[i + 1] = shifted;
    }
    mBoundaries[segCount] = 1.0f;

    mLastSkew = skew;
    mLastSegCount = segCount;
    mBoundariesDirty = false;
  }

  // Check if boundaries need recomputing (callable from any thread)
  void Etcher::checkBoundariesDirty()
  {
    float skew = CLAMP(-1.0f, 1.0f, mSkew.value());
    int segCount = CLAMP(2, kMaxSegments, (int)(mSegmentCount.value() + 0.5f));

    if (skew != mLastSkew || segCount != mLastSegCount)
    {
      mCachedSegmentCount = segCount;
      mBoundariesDirty = true;
    }

    if (mBoundariesDirty)
      recomputeBoundaries();
  }

  int Etcher::getSegmentCount()
  {
    checkBoundariesDirty();
    return mCachedSegmentCount;
  }

  float Etcher::getSegmentBoundary(int i)
  {
    checkBoundariesDirty();
    return mBoundaries[CLAMP(0, mCachedSegmentCount, i)];
  }

  // --- Interpolation ---

  float Etcher::interpolateSegment(int seg, float frac)
  {
    Internal &s = *mpInternal;
    int segCount = mCachedSegmentCount;

    // Use deviation curve override if active, else segment's own curve
    int curveType = (mDevCurveSnap >= 0) ? mDevCurveSnap : s.curve[seg];

    float a = s.offset[seg];

    // Last segment: no next segment to interpolate toward
    if (seg >= segCount - 1)
      return a;

    float b = s.offset[seg + 1];

    switch (curveType)
    {
    case CURVE_NONE:
      return a;

    case CURVE_LINEAR:
      return a + frac * (b - a);

    case CURVE_CUBIC:
    {
      // Catmull-Rom spline with clamped tangents at boundaries
      float prev = (seg > 0) ? s.offset[seg - 1] : a;
      float next2 = (seg + 2 < segCount) ? s.offset[seg + 2] : b;

      float t0 = (b - prev) * 0.5f;
      float t1 = (next2 - a) * 0.5f;

      float f2 = frac * frac;
      float f3 = f2 * frac;

      // Hermite basis
      return (2.0f * f3 - 3.0f * f2 + 1.0f) * a +
             (f3 - 2.0f * f2 + frac) * t0 +
             (-2.0f * f3 + 3.0f * f2) * b +
             (f3 - f2) * t1;
    }

    default:
      return a;
    }
  }

  // --- Evaluate (callable from UI thread for curve drawing) ---

  float Etcher::evaluate(float normalizedInput)
  {
    checkBoundariesDirty();

    int segCount = mCachedSegmentCount;
    float pos = CLAMP(0.0f, 1.0f, normalizedInput);

    // Linear scan to find active segment
    int seg = segCount - 1;
    for (int i = 0; i < segCount; i++)
    {
      if (pos < mBoundaries[i + 1])
      {
        seg = i;
        break;
      }
    }

    // Fraction within segment
    float segStart = mBoundaries[seg];
    float segEnd = mBoundaries[seg + 1];
    float span = segEnd - segStart;
    float frac = (span > 0.0001f) ? (pos - segStart) / span : 0.0f;

    // Bypass deviation for clean curve drawing
    int savedCurveSnap = mDevCurveSnap;
    mDevCurveSnap = -1;
    float result = interpolateSegment(seg, frac);
    mDevCurveSnap = savedCurveSnap;
    return result;
  }

  // --- Deviation ---

  void Etcher::rollDeviation(int seg)
  {
    float dev = CLAMP(0.0f, 1.0f, mDeviation.value());
    int scope = CLAMP(0, (int)DEV_COUNT - 1, (int)(mDeviationScope.value() + 0.5f));

    mDevOffsetSnap = 0.0f;
    mDevCurveSnap = -1;
    mDevWeightSnap = 0.0f;

    if (dev < 0.001f)
      return;

    bool doOffset = (scope == DEV_OFFSET || scope == DEV_ALL);
    bool doCurve = (scope == DEV_CURVE || scope == DEV_ALL);
    bool doWeight = (scope == DEV_WEIGHT || scope == DEV_ALL);

    if (doOffset)
    {
      // Scale: deviation 1.0 = up to +/-5V random offset
      mDevOffsetSnap = randFloat() * dev * 1.0f;
    }

    if (doCurve)
    {
      // Probability of switching curve type
      float roll = (randFloat() + 1.0f) * 0.5f; // 0..1
      if (roll < dev)
      {
        // Pick a random curve type different from the segment's own
        sRandState = sRandState * 1664525u + 1013904223u;
        mDevCurveSnap = (int)(sRandState % (uint32_t)CURVE_COUNT);
      }
    }

    if (doWeight)
    {
      // Perturb the boundary: deviation 1.0 = up to +/-2.0 weight offset
      mDevWeightSnap = randFloat() * dev * 2.0f;
    }
  }

  // --- Transform ---

  void Etcher::fireTransform()
  {
    mManualFire = true;
  }

  void Etcher::applyTransform()
  {
    Internal &s = *mpInternal;
    int func = CLAMP(0, 7, (int)(mTransformFunc.value() + 0.5f));
    float depth = CLAMP(0.0f, 1.0f, mTransformDepth.value());
    int segCount = mCachedSegmentCount;

    switch (func)
    {
    case 0: // Random: lerp toward random values
      for (int i = 0; i < segCount; i++)
      {
        float oTarget = randFloat(); // -1 to 1
        s.offset[i] += (oTarget - s.offset[i]) * depth;
        float wTarget = 0.1f + (randFloat() + 1.0f) * 0.5f * 3.9f;
        s.weight[i] += (wTarget - s.weight[i]) * depth;
      }
      break;

    case 1: // Rotate: shift segments by depth-scaled positions
    {
      int rot = 1 + (int)(depth * (float)(segCount - 1));
      rot = rot % segCount;
      if (rot == 0) rot = 1;
      for (int i = 0; i < segCount; i++)
      {
        int src = (i + rot) % segCount;
        s.tmpOffset[i] = s.offset[src];
        s.tmpCurve[i] = s.curve[src];
        s.tmpWeight[i] = s.weight[src];
      }
      memcpy(s.offset, s.tmpOffset, segCount * sizeof(float));
      memcpy(s.curve, s.tmpCurve, segCount * sizeof(int));
      memcpy(s.weight, s.tmpWeight, segCount * sizeof(float));
      break;
    }

    case 2: // Invert: flip offsets
      for (int i = 0; i < segCount; i++)
      {
        float target = -s.offset[i];
        s.offset[i] += (target - s.offset[i]) * depth;
      }
      break;

    case 3: // Reverse: swap segment order
      for (int i = 0; i < segCount / 2; i++)
      {
        int j = segCount - 1 - i;
        float oA = s.offset[i], oB = s.offset[j];
        s.offset[i] += (oB - oA) * depth;
        s.offset[j] += (oA - oB) * depth;
        float wA = s.weight[i], wB = s.weight[j];
        s.weight[i] += (wB - wA) * depth;
        s.weight[j] += (wA - wB) * depth;
        if (depth > 0.99f)
        {
          int cTmp = s.curve[i];
          s.curve[i] = s.curve[j];
          s.curve[j] = cTmp;
        }
      }
      break;

    case 4: // Smooth: average with neighbors
    {
      for (int i = 0; i < segCount; i++)
      {
        int prev = (i > 0) ? i - 1 : segCount - 1;
        int next = (i < segCount - 1) ? i + 1 : 0;
        float avg = (s.offset[prev] + s.offset[i] + s.offset[next]) / 3.0f;
        s.tmpOffset[i] = s.offset[i] + (avg - s.offset[i]) * depth;
      }
      memcpy(s.offset, s.tmpOffset, segCount * sizeof(float));
      break;
    }

    case 5: // Quantize: snap to N levels
    {
      float levels = 2.0f + depth * 14.0f; // 2-16 levels
      for (int i = 0; i < segCount; i++)
      {
        // Map -1..1 to 0..1, quantize, map back
        float norm = (s.offset[i] + 1.0f) * 0.5f;
        float q = floorf(norm * levels + 0.5f) / levels;
        float target = q * 2.0f - 1.0f;
        s.offset[i] += (target - s.offset[i]) * depth;
      }
      break;
    }

    case 6: // Spread: normalize to full range
    {
      float mn = s.offset[0], mx = s.offset[0];
      for (int i = 1; i < segCount; i++)
      {
        if (s.offset[i] < mn) mn = s.offset[i];
        if (s.offset[i] > mx) mx = s.offset[i];
      }
      float range = mx - mn;
      if (range > 0.001f)
      {
        for (int i = 0; i < segCount; i++)
        {
          float norm = (s.offset[i] - mn) / range * 2.0f - 1.0f;
          s.offset[i] += (norm - s.offset[i]) * depth;
        }
      }
      break;
    }

    case 7: // Fold: wrap values back into range
      for (int i = 0; i < segCount; i++)
      {
        float v = s.offset[i] * (1.0f + depth * 2.0f); // amplify then fold
        while (v > 1.0f) v = 2.0f - v;
        while (v < -1.0f) v = -2.0f - v;
        s.offset[i] += (v - s.offset[i]) * depth;
      }
      break;
    }

    // Clamp to valid ranges
    for (int i = 0; i < segCount; i++)
    {
      s.offset[i] = CLAMP(-1.0f, 1.0f, s.offset[i]);
      s.weight[i] = CLAMP(0.1f, 4.0f, s.weight[i]);
    }

    mBoundariesDirty = true;
    loadSegment(mActiveSegment);
  }

  // --- Process ---

  void Etcher::process()
  {
    float *in = mInput.buffer();
    float *out = mOut.buffer();
    float *xform = mTransform.buffer();

    float level = mLevel.value();

    // Transform gate (block-rate check)
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool xformHigh = xform[i] > 0.0f;
      if ((xformHigh && !mTransformWasHigh) || mManualFire)
      {
        applyTransform();
        mManualFire = false;
      }
      mTransformWasHigh = xformHigh;
    }

    checkBoundariesDirty();

    int segCount = mCachedSegmentCount;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float input = CLAMP(-1.0f, 1.0f, in[i]);
      float normalized = (input + 1.0f) * 0.5f; // map -1..+1 to 0..1

      // Linear scan to find active segment
      int seg = segCount - 1;
      for (int j = 0; j < segCount; j++)
      {
        if (normalized < mBoundaries[j + 1])
        {
          seg = j;
          break;
        }
      }

      // Segment transition: roll new deviation snapshot
      if (seg != mLastActiveSegment)
      {
        rollDeviation(seg);
        mLastActiveSegment = seg;
      }

      // Fraction within segment
      float segStart = mBoundaries[seg];
      float segEnd = mBoundaries[seg + 1];

      // Weight deviation: shift the end boundary for this segment
      // This stretches or compresses the active segment's span
      if (mDevWeightSnap != 0.0f)
      {
        float totalRange = 1.0f;
        float adjustedEnd = segEnd + mDevWeightSnap * (segEnd - segStart);
        adjustedEnd = CLAMP(segStart + 0.001f, totalRange, adjustedEnd);
        segEnd = adjustedEnd;
      }

      float span = segEnd - segStart;
      float frac = (span > 0.0001f) ? (normalized - segStart) / span : 0.0f;
      frac = CLAMP(0.0f, 1.0f, frac);

      // interpolateSegment uses mDevCurveSnap if set
      float output = interpolateSegment(seg, frac) * level;

      // Offset deviation: add to output
      output += mDevOffsetSnap;

      out[i] = CLAMP(-1.0f, 1.0f, output);

      mActiveSegment = seg;
    }

    // Store last sample's state for UI
    mCurrentInput = CLAMP(-1.0f, 1.0f, in[FRAMELENGTH - 1]);
    mCurrentOutput = out[FRAMELENGTH - 1];
  }

} // namespace stolmine
