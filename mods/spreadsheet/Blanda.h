// Blanda -- three-input scan mixer (working name, Swedish for "mingle")
//
// Each input feeds a triangular bell centered on a per-input Offset (0..1).
// A global Scan CV sweeps a playhead across that axis; the mix at any
// moment is sum(in[i] * coef[i] * level[i]) where coef[i] is the bell
// evaluated at scan. Per-input Weight scales the bell's width relative
// to a global bipolar Focus (-1 = collapse to spikes, +1 = full overlap).
//
// Distinct from FadeMixer (generic 4-input equal-power crossfade), Parfait/
// Impasto (single input through a crossover), and anything in the package:
// the scan+focus pair plus per-input Weight/Offset lets you build
// overlapping or isolated per-input zones on the scan axis and morph
// through them.

#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>

namespace stolmine
{
  static const int kBlandaInputs = 3;

  class Blanda : public od::Object
  {
  public:
    Blanda();
    virtual ~Blanda();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn1{"In1"};
    od::Inlet mIn2{"In2"};
    od::Inlet mIn3{"In3"};
    od::Outlet mOut{"Out"};

    // Per-input mixing params
    od::Parameter mLevel0{"Level0", 1.0f};
    od::Parameter mLevel1{"Level1", 1.0f};
    od::Parameter mLevel2{"Level2", 1.0f};
    od::Parameter mWeight0{"Weight0", 1.0f};
    od::Parameter mWeight1{"Weight1", 1.0f};
    od::Parameter mWeight2{"Weight2", 1.0f};
    od::Parameter mOffset0{"Offset0", 0.1667f};
    od::Parameter mOffset1{"Offset1", 0.5f};
    od::Parameter mOffset2{"Offset2", 0.8333f};
    od::Parameter mShape0{"Shape0", 0.0f}; // 0 = triangle, 1 = plateau
    od::Parameter mShape1{"Shape1", 0.0f};
    od::Parameter mShape2{"Shape2", 0.0f};

    // Global
    od::Parameter mScan{"Scan", 0.5f};
    od::Parameter mFocus{"Focus", 0.0f};
    od::Parameter mOutputLevel{"OutputLevel", 1.0f};
#endif

    // SWIG-visible getters -- graphics read these in draw().
    // All reflect state cached at the end of the last process() block.
    float getScanPos();
    float getFocus();
    float getFocusWidth();
    float getInputOffset(int i);
    float getInputWeight(int i);
    float getInputLevel(int i);
    float getInputShape(int i);
    float getMixCoef(int i);

  private:
#ifndef SWIGLUA
    // Cached for the graphic, updated per-block.
    float mLastScan = 0.5f;
    float mLastFocus = 0.0f;
    float mLastFocusWidth = 0.22f;
    float mLastOffset[kBlandaInputs] = {0.1667f, 0.5f, 0.8333f};
    float mLastWeight[kBlandaInputs] = {1.0f, 1.0f, 1.0f};
    float mLastLevel[kBlandaInputs]  = {1.0f, 1.0f, 1.0f};
    float mLastShape[kBlandaInputs]  = {0.0f, 0.0f, 0.0f};
    float mLastCoef[kBlandaInputs]   = {0.0f, 0.0f, 0.0f};
#endif
  };

} // namespace stolmine
