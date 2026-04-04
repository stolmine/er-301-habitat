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
    float attackSec = CLAMP(0.0001f, 0.5f, mAttack.value());
    float decaySec = CLAMP(0.0001f, 5.0f, mDecay.value());

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

    // Envelope coefficients (within 1% in attack/release time)
    if (attackSec < 0.5f * globalConfig.framePeriod) attackSec = 0.5f * globalConfig.framePeriod;
    if (decaySec < 0.5f * globalConfig.framePeriod) decaySec = 0.5f * globalConfig.framePeriod;

    float attCoeff = expf(logf(0.01f) / (attackSec * sr));
    float decCoeff = expf(logf(0.01f) / (decaySec * sr));
    float att2 = 1.0f - attCoeff;
    float dec2 = 1.0f - decCoeff;

    // Adaptive threshold (slewed average of rectified BPF output)
    float slow = (attackSec > decaySec ? attackSec : decaySec) * 3.0f;
    float slowCoeff = expf(logf(0.01f) / (slow * globalConfig.frameRate));
    float slow2 = 1.0f - slowCoeff;

    // First pass: BPF + accumulate rectified sum for threshold
    float rectSum = 0.0f;
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i];
      float y = b0 * x + b1 * mX1 + b2 * mX2 - a1 * mY1 - a2 * mY2;
      mX2 = mX1;
      mX1 = x;
      mY2 = mY1;
      mY1 = y;
      out[i] = y; // temporarily store BPF output
      rectSum += fabsf(y);
    }

    mThreshold = slow2 * rectSum / FRAMELENGTH + slowCoeff * mThreshold;

    // Second pass: envelope follow using adaptive threshold
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float rect = fabsf(out[i]);
      if (rect > mThreshold)
        mEnv = att2 * rect + attCoeff * mEnv;
      else
        mEnv = dec2 * rect + decCoeff * mEnv;
      out[i] = mEnv;
    }
  }

} // namespace stolmine
