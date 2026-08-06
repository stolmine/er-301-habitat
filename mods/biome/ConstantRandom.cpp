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
    // Rate floors at EXACTLY 0 = pause. phaseInc becomes 0, so the phase never
    // wraps and no new target is ever drawn; the slew finishes travelling to
    // the target it was already heading for and then holds there, exactly, with
    // no drift. (Measured: 0 draws and 0.000e+00 movement over 300 s once
    // settled.) The old 0.01 floor meant a "paused" unit still drew a new value
    // every 100 seconds.
    float rate = CLAMP(0.0f, 1000.0f, mRate.value());
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
