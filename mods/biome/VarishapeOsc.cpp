#include "VarishapeOsc.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

#ifndef TEST
#define TEST
#endif
#include <stages/variable_shape_oscillator.h>

namespace stolmine
{

  VarishapeOsc::VarishapeOsc()
  {
    addInput(mVOct);
    addInput(mSync);
    addOutput(mOutput);
    addParameter(mShape);
    addParameter(mFundamental);

    mpOsc = new stages::VariableShapeOscillator();
    mpOsc->Init();
    mpWorkBuffer = new float[FRAMELENGTH];
  }

  VarishapeOsc::~VarishapeOsc()
  {
    delete mpOsc;
    delete[] mpWorkBuffer;
  }

  void VarishapeOsc::process()
  {
    float *voct = mVOct.buffer();
    float *out = mOutput.buffer();

    float sr = globalConfig.sampleRate;
    float f0 = CLAMP(0.1f, sr * 0.49f, mFundamental.value());
    float shape = CLAMP(0.0f, 1.0f, mShape.value());

    // V/Oct pitch (block-rate)
    float pitch = voct[0] * 10.0f;
    float freq = f0 * powf(2.0f, pitch);
    float freqNorm = freq / sr;

    // Render oscillator
    mpOsc->Render(freqNorm, shape, mpWorkBuffer, FRAMELENGTH);

    for (int i = 0; i < FRAMELENGTH; i++)
      out[i] = mpWorkBuffer[i];
  }

} // namespace stolmine
