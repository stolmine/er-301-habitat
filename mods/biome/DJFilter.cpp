#include "DJFilter.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{

  DJFilter::DJFilter()
  {
    addInput(mInput);
    addOutput(mOutput);
    addParameter(mCut);
    addParameter(mQ);
  }

  DJFilter::~DJFilter()
  {
  }

  void DJFilter::process()
  {
    float *in = mInput.buffer();
    float *out = mOutput.buffer();

    float cut = CLAMP(-1.0f, 1.0f, mCut.value());
    float q = CLAMP(0.0f, 1.0f, mQ.value());
    float sr = globalConfig.sampleRate;

    // Determine filter mode and cutoff
    // Center (cut=0) = bypass
    // Negative = LP with cutoff sweeping down from 20kHz to 20Hz
    // Positive = HP with cutoff sweeping up from 20Hz to 20kHz
    float absCut = fabsf(cut);

    if (absCut < 0.01f)
    {
      // Near center: bypass
      for (int i = 0; i < FRAMELENGTH; i++)
        out[i] = in[i];
      return;
    }

    // Map |cut| to frequency: exponential sweep 20Hz to 20kHz
    float freq;
    if (cut < 0.0f)
    {
      // LP: 1.0 = 20kHz, approach 0 = 20Hz
      freq = 20.0f * powf(1000.0f, 1.0f - absCut);
    }
    else
    {
      // HP: 0 = 20Hz, approach 1.0 = 20kHz
      freq = 20.0f * powf(1000.0f, absCut);
    }

    // SVF coefficients (Cytomic/Vadim)
    float g = tanf(3.14159f * CLAMP(20.0f, sr * 0.49f, freq) / sr);
    float k = 2.0f - 2.0f * q * 0.95f; // Q: 0.5 (clean) to ~20 (resonant)
    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;

    bool isLP = (cut < 0.0f);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float v0 = in[i];
      float v3 = v0 - mIc2eq;
      float v1 = a1 * mIc1eq + a2 * v3;
      float v2 = mIc2eq + a2 * mIc1eq + a3 * v3;
      mIc1eq = 2.0f * v1 - mIc1eq;
      mIc2eq = 2.0f * v2 - mIc2eq;

      // LP = v2, HP = v0 - k*v1 - v2
      // Crossfade with dry based on distance from center for smooth transition
      float wet = isLP ? v2 : (v0 - k * v1 - v2);
      float blend = absCut; // 0 at center (dry), 1 at extremes (full filter)
      out[i] = in[i] * (1.0f - blend) + wet * blend;
    }
  }

} // namespace stolmine
