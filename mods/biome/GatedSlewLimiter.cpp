#include "GatedSlewLimiter.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

namespace stolmine
{

  GatedSlewLimiter::GatedSlewLimiter()
  {
    addInput(mInput);
    addInput(mGate);
    addOutput(mOutput);
    addParameter(mTime);
    addOption(mDirection);
  }

  GatedSlewLimiter::~GatedSlewLimiter()
  {
  }

  void GatedSlewLimiter::process()
  {
    float *in = mInput.buffer();
    float *gate = mGate.buffer();
    float *out = mOutput.buffer();

    float rate = 1.0f / CLAMP(0.003f, 1000.0f, mTime.value());
    float maxDiffPerSample = rate * globalConfig.samplePeriod;
    int dir = mDirection.value();

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i];

      if (gate[i] > 0.5f)
      {
        // Gate high: apply slew
        float diff = x - mPreviousValue;
        switch (dir)
        {
        case CHOICE_UP:
          if (diff > 0.0f && diff > maxDiffPerSample)
            diff = maxDiffPerSample;
          break;
        case CHOICE_BOTH:
          diff = CLAMP(-maxDiffPerSample, maxDiffPerSample, diff);
          break;
        case CHOICE_DOWN:
          if (diff < 0.0f && diff < -maxDiffPerSample)
            diff = -maxDiffPerSample;
          break;
        }
        mPreviousValue += diff;
      }
      else
      {
        // Gate low: pass through, track input
        mPreviousValue = x;
      }

      out[i] = mPreviousValue;
    }
  }

} // namespace stolmine
