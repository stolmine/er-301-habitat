#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>

namespace stolmine
{

  class Som : public od::Object
  {
  public:
    Som();
    virtual ~Som();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Outlet mOutL{"Out1"};
    od::Outlet mOutR{"Out2"};

    od::Parameter mPlasticity{"Plasticity", 0.0f};
    od::Parameter mScanPos{"ScanPos", 0.0f};
    od::Parameter mMix{"Mix", 0.5f};
    od::Parameter mOutputLevel{"OutputLevel", 1.0f};
    od::Parameter mParallax{"Parallax", 0.0f};
    od::Parameter mModAmount{"ModAmount", 0.0f};
    od::Parameter mModRate{"ModRate", 0.1f};
    od::Parameter mModShape{"ModShape", 0.0f};
    od::Parameter mModFeedback{"ModFeedback", 0.0f};
    od::Parameter mNeighborhoodRadius{"NeighborhoodRadius", 0.06f};
    od::Parameter mLearningRate{"LearningRate", 0.1f};
    od::Parameter mFeedback{"Feedback", 0.0f};
#endif

    float getNodeWeight(int node, int dim);
    float getNodeActivation(int node);
    int getBMU();
    float getOutputSample(int idx);
    int getNodeCount() { return 64; }
    float getNodeX(int node);
    float getNodeY(int node);
    float getNodeZ(int node);
    float getVoiceStateA();
    float getVoiceStateB();
    int getScanNode();

#ifndef SWIGLUA
  private:
    struct Internal;
    Internal *mpInternal;

    int mBMU = 0;
    int mScanNode = 0;
#endif
  };

} // namespace stolmine
