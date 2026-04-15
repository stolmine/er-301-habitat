// Blanda -- three-input scan mixer implementation.

#include "Blanda.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{
  // Bell half-width at focus = 0. Picked so adjacent inputs at offsets
  // 0.17 / 0.5 / 0.83 touch at coef ~ 0.5 with weight = 1.
  static const float kFocusBaseline = 0.22f;

  // Floor so a "collapsed" bell is still one scan unit wide. Prevents
  // div-by-zero and keeps the playhead from falling between cracks when
  // focus is heavily negative.
  static const float kMinWidth = 1.0f / 512.0f;

  Blanda::Blanda()
  {
    addInput(mIn1);
    addInput(mIn2);
    addInput(mIn3);
    addOutput(mOut);
    addParameter(mLevel0);
    addParameter(mLevel1);
    addParameter(mLevel2);
    addParameter(mWeight0);
    addParameter(mWeight1);
    addParameter(mWeight2);
    addParameter(mOffset0);
    addParameter(mOffset1);
    addParameter(mOffset2);
    addParameter(mScan);
    addParameter(mFocus);
    addParameter(mOutputLevel);
  }

  Blanda::~Blanda() {}

  float Blanda::getScanPos()     { return mLastScan; }
  float Blanda::getFocus()       { return mLastFocus; }
  float Blanda::getFocusWidth()  { return mLastFocusWidth; }
  float Blanda::getInputOffset(int i) { return mLastOffset[CLAMP(0, kBlandaInputs - 1, i)]; }
  float Blanda::getInputWeight(int i) { return mLastWeight[CLAMP(0, kBlandaInputs - 1, i)]; }
  float Blanda::getInputLevel(int i)  { return mLastLevel[CLAMP(0, kBlandaInputs - 1, i)]; }
  float Blanda::getMixCoef(int i)     { return mLastCoef[CLAMP(0, kBlandaInputs - 1, i)]; }

  void Blanda::process()
  {
    float *in1 = mIn1.buffer();
    float *in2 = mIn2.buffer();
    float *in3 = mIn3.buffer();
    float *out = mOut.buffer();

    // Read params once per block -- they update at block rate from the
    // tied ParameterAdapters. (Per-sample CV appears as the adapter's Out
    // signal; since we tie() to op parameters, the parameter value itself
    // is already the per-sample value when read via Parameter::value().
    // For simplicity in v0 we treat them as block-rate; upgrade to
    // per-sample reads if scan CV needs tighter tracking.)
    float level[kBlandaInputs] = {
        CLAMP(0.0f, 4.0f, mLevel0.value()),
        CLAMP(0.0f, 4.0f, mLevel1.value()),
        CLAMP(0.0f, 4.0f, mLevel2.value())
    };
    float weight[kBlandaInputs] = {
        CLAMP(0.0f, 4.0f, mWeight0.value()),
        CLAMP(0.0f, 4.0f, mWeight1.value()),
        CLAMP(0.0f, 4.0f, mWeight2.value())
    };
    float offset[kBlandaInputs] = {
        CLAMP(0.0f, 1.0f, mOffset0.value()),
        CLAMP(0.0f, 1.0f, mOffset1.value()),
        CLAMP(0.0f, 1.0f, mOffset2.value())
    };
    float focus = CLAMP(-1.0f, 1.0f, mFocus.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());

    // focus bipolar: -1 -> focusWidth = kFocusBaseline/4, +1 -> 4x wider.
    // powf(4, focus) = exp(focus * ln 4).
    float focusWidth = kFocusBaseline * powf(4.0f, focus);

    // Precompute per-band widths.
    float w[kBlandaInputs];
    for (int b = 0; b < kBlandaInputs; b++)
    {
      float ww = weight[b] * focusWidth;
      if (ww < kMinWidth) ww = kMinWidth;
      w[b] = ww;
    }

    float scan = CLAMP(0.0f, 1.0f, mScan.value());

    float coef[kBlandaInputs];
    for (int b = 0; b < kBlandaInputs; b++)
    {
      float d = scan - offset[b];
      if (d < 0.0f) d = -d;
      float c = 1.0f - d / w[b];
      coef[b] = (c > 0.0f) ? c : 0.0f;
    }

    // Precompute per-input scalars so the inner loop is three multiplies.
    float s0 = coef[0] * level[0];
    float s1 = coef[1] * level[1];
    float s2 = coef[2] * level[2];

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float y = in1[i] * s0 + in2[i] * s1 + in3[i] * s2;
      out[i] = y * outputLevel;
    }

    // Cache for the graphic.
    mLastScan = scan;
    mLastFocus = focus;
    mLastFocusWidth = focusWidth;
    for (int b = 0; b < kBlandaInputs; b++)
    {
      mLastOffset[b] = offset[b];
      mLastWeight[b] = weight[b];
      mLastLevel[b]  = level[b];
      mLastCoef[b]   = coef[b];
    }
  }

} // namespace stolmine
