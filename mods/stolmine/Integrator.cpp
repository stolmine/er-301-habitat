#include "Integrator.h"
#include <od/config.h>
#include <hal/ops.h>

namespace stolmine
{

  Integrator::Integrator()
  {
    addInput(mInput);
    addInput(mReset);
    addOutput(mOutput);
    addParameter(mRate);
    addParameter(mLeak);
  }

  Integrator::~Integrator()
  {
  }

  void Integrator::process()
  {
    float *in = mInput.buffer();
    float *reset = mReset.buffer();
    float *out = mOutput.buffer();

    float rate = CLAMP(0.0f, 100.0f, mRate.value());
    float leak = CLAMP(0.0f, 1.0f, mLeak.value());
    float dt = globalConfig.samplePeriod;

    // Leak coefficient: per-sample decay toward zero
    // leak=0: no decay (pure integrator), leak=1: ~10ms decay
    float leakCoeff = leak * 50.0f * dt;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Reset on rising edge
      bool high = reset[i] > 0.5f;
      if (high && !mResetWasHigh)
        mValue = 0.0f;
      mResetWasHigh = high;

      // Integrate
      mValue += in[i] * rate * dt;

      // Leak toward zero
      mValue -= mValue * leakCoeff;

      // Clip to +/-5V
      mValue = CLAMP(-5.0f, 5.0f, mValue);

      out[i] = mValue;
    }
  }

} // namespace stolmine
