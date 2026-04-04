#include "SpectralFollower.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{

  SpectralFollower::SpectralFollower()
  {
    addInput(mInput);
    addOutput(mOutput);
    addParameter(mFreq);
    addParameter(mBandwidth);
    addParameter(mAttack);
    addParameter(mDecay);
  }

  SpectralFollower::~SpectralFollower()
  {
  }

  void SpectralFollower::process()
  {
    float *in = mInput.buffer();
    float *out = mOutput.buffer();
    float sr = globalConfig.sampleRate;

    float freq = CLAMP(20.0f, sr * 0.49f, mFreq.value());
    float bw = CLAMP(0.1f, 4.0f, mBandwidth.value());
    float attackMs = CLAMP(0.1f, 500.0f, mAttack.value());
    float decayMs = CLAMP(0.1f, 5000.0f, mDecay.value());

    // Biquad BPF coefficients (RBJ cookbook)
    float w0 = 2.0f * 3.14159265f * freq / sr;
    float sinW0 = sinf(w0);
    float cosW0 = cosf(w0);
    float alpha = sinW0 * sinhf(0.693147f * bw * w0 / sinW0); // ln(2) * BW

    float b0 = alpha;
    float b1 = 0.0f;
    float b2 = -alpha;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosW0;
    float a2 = 1.0f - alpha;

    // Normalize
    b0 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;

    // Envelope coefficients
    float attCoeff = 1.0f - expf(-1.0f / (attackMs * 0.001f * sr));
    float decCoeff = 1.0f - expf(-1.0f / (decayMs * 0.001f * sr));

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i];

      // BPF
      float y = b0 * x + b1 * mX1 + b2 * mX2 - a1 * mY1 - a2 * mY2;
      mX2 = mX1;
      mX1 = x;
      mY2 = mY1;
      mY1 = y;

      // Rectify + envelope follow
      float rect = fabsf(y);
      float coeff = (rect > mEnv) ? attCoeff : decCoeff;
      mEnv += (rect - mEnv) * coeff;

      out[i] = mEnv;
    }
  }

} // namespace stolmine
