#include "TiltEQ.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{

  TiltEQ::TiltEQ()
  {
    addInput(mInput);
    addOutput(mOutput);
    addParameter(mTilt);
  }

  TiltEQ::~TiltEQ()
  {
  }

  void TiltEQ::process()
  {
    float *in = mInput.buffer();
    float *out = mOutput.buffer();

    // Tilt: -1 = dark (boost lows, cut highs), 0 = flat, +1 = bright
    float tilt = CLAMP(-1.0f, 1.0f, mTilt.value());

    // One-pole crossover at ~800Hz
    float sr = globalConfig.sampleRate;
    float fc = 800.0f / sr;
    float coeff = 1.0f / (1.0f + 1.0f / (2.0f * 3.14159f * fc));

    // Tilt gain: 6dB per unit tilt
    float gain = powf(10.0f, tilt * 0.3f); // +/-6dB
    float lgain = 1.0f / gain;             // complementary

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i];
      mLpState += (x - mLpState) * coeff;
      float lp = mLpState;
      float hp = x - lp;
      out[i] = lp * lgain + hp * gain;
    }
  }

} // namespace stolmine
