#include "VarishapeVoice.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

#ifndef TEST
#define TEST
#endif
#include <stages/variable_shape_oscillator.h>
#include <plaits/dsp/envelope.h>

namespace stolmine
{

  VarishapeVoice::VarishapeVoice()
  {
    addInput(mVOct);
    addInput(mSync);
    addInput(mGate);
    addOutput(mOutput);
    addParameter(mShape);
    addParameter(mFundamental);
    addParameter(mDecay);

    mpOsc = new stages::VariableShapeOscillator();
    mpOsc->Init();
    mpEnv = new plaits::DecayEnvelope();
    mpEnv->Init();
    mpWorkBuffer = new float[FRAMELENGTH];
  }

  VarishapeVoice::~VarishapeVoice()
  {
    delete mpOsc;
    delete mpEnv;
    delete[] mpWorkBuffer;
  }

  void VarishapeVoice::process()
  {
    float *voct = mVOct.buffer();
    float *gate = mGate.buffer();
    float *out = mOutput.buffer();

    float sr = globalConfig.sampleRate;
    float f0 = CLAMP(0.1f, sr * 0.49f, mFundamental.value());
    float shape = CLAMP(0.0f, 1.0f, mShape.value());

    // Decay: 0 = fast pluck, 1 = long sustain
    float decayParam = CLAMP(0.0f, 1.0f, mDecay.value());
    float decayCoeff = 0.0001f + (1.0f - decayParam) * 0.05f;

    // Gate edge detection
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool gateHigh = gate[i] > 0.5f;
      if (gateHigh && !mGateWasHigh)
        mpEnv->Trigger();
      mGateWasHigh = gateHigh;
    }

    // V/Oct pitch (block-rate)
    float pitch = voct[0] * 10.0f;
    float freq = f0 * powf(2.0f, pitch);
    float freqNorm = freq / sr;

    // Render oscillator
    mpOsc->Render(freqNorm, shape, mpWorkBuffer, FRAMELENGTH);

    // Apply decay envelope
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      mpEnv->Process(decayCoeff);
      out[i] = mpWorkBuffer[i] * mpEnv->value();
    }
  }

} // namespace stolmine
