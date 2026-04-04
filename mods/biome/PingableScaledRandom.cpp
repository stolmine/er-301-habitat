#include "PingableScaledRandom.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{

  PingableScaledRandom::PingableScaledRandom()
  {
    addInput(mTrigger);
    addOutput(mOutput);
    addParameter(mScale);
    addParameter(mOffset);
    addParameter(mLevels);
  }

  PingableScaledRandom::~PingableScaledRandom()
  {
  }

  void PingableScaledRandom::process()
  {
    float *trig = mTrigger.buffer();
    float *out = mOutput.buffer();

    float scale = mScale.value();
    float offset = mOffset.value();
    int levels = (int)(mLevels.value() + 0.5f);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Rising edge detection
      bool high = trig[i] > 0.5f;
      if (high && !mTrigWasHigh)
      {
        // Generate bipolar random -1 to +1
        float r = od::Random::generateFloat(-1.0f, 1.0f);

        // Quantize if levels > 0
        if (levels > 1)
        {
          r = floorf(r * (float)levels + 0.5f) / (float)levels;
        }

        mHeldValue = r * scale + offset;
      }
      mTrigWasHigh = high;

      out[i] = mHeldValue;
    }
  }

} // namespace stolmine
