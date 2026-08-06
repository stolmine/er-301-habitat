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
    // Slew is now the built-in slew TIME in seconds, matching the framework's
    // slewTimes control wholesale, rather than a 0-1 "amount". The old 0-1 law
    // (coeff = (1-s)*50 + 1) was badly bunched: the whole middle half of the
    // dial only covered 22..39 ms, and the top of the throw stopped at 1 s.
    float slewTime = MAX(0.0f, mSlew.value());
    // Bipolar to match the built-in [-1,1] level map: a negative level
    // inverts the random rather than muting it.
    float level = CLAMP(-1.0f, 1.0f, mLevel.value());
    float dt = globalConfig.samplePeriod;

    // One-pole toward the target with the requested time constant. A time of 0
    // (or anything under a sample) is a hard jump = pure sample-and-hold, which
    // preserves what the old slew=0 default did.
    float alpha = (slewTime > dt) ? (1.0f - expf(-dt / slewTime)) : 1.0f;

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
