#include "ConstantRandom.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{

  ConstantRandom::ConstantRandom()
  {
    addOutput(mOutput);
    addParameter(mRate);
    addParameter(mSlew);
    addParameter(mLevel);
  }

  ConstantRandom::~ConstantRandom() {}

  void ConstantRandom::process()
  {
    float *out = mOutput.buffer();
    float rate = CLAMP(0.01f, 1000.0f, mRate.value());
    float slew = CLAMP(0.0f, 1.0f, mSlew.value());
    float level = CLAMP(0.0f, 1.0f, mLevel.value());
    float dt = globalConfig.samplePeriod;

    // Slew coefficient: 0 = instant (pure S&H), 1 = very smooth (~50ms)
    float slewCoeff = (slew > 0.001f) ? (1.0f - slew) * 50.0f + 1.0f : 10000.0f;
    float alpha = 1.0f - expf(-slewCoeff * dt);

    float phaseInc = rate * dt;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      mPhase += phaseInc;
      if (mPhase >= 1.0f)
      {
        mPhase -= 1.0f;
        // New random target (bipolar -1..+1)
        mSeed = mSeed * 1664525u + 1013904223u;
        mTarget = (float)((int32_t)mSeed) / (float)0x7FFFFFFF;
      }

      // Slew toward target
      mValue += (mTarget - mValue) * alpha;

      out[i] = mValue * level;
    }
  }

} // namespace stolmine
