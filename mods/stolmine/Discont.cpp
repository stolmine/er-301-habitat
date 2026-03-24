// 94 Discont — 7-mode waveshaper/saturation
// Ported from monokit SuperCollider implementation

#include "Discont.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{

  static inline float fold2(float x) {
    // Symmetric wavefolding at ±1
    while (x > 1.0f || x < -1.0f) {
      if (x > 1.0f) x = 2.0f - x;
      if (x < -1.0f) x = -2.0f - x;
    }
    return x;
  }

  Discont::Discont()
  {
    addInput(mIn);
    addOutput(mOut);
    addParameter(mAmount);
    addParameter(mMix);
    addParameter(mMode);
  }

  Discont::~Discont() {}

  void Discont::process()
  {
    float *in = mIn.buffer();
    float *out = mOut.buffer();

    float amount = CLAMP(0.0f, 4.0f, mAmount.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    int mode = CLAMP(0, 6, (int)(mMode.value() + 0.5f));

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float dry = in[i];
      float sig = dry * amount;
      float wet;

      switch (mode) {
        case 0: // Fold
          wet = fold2(sig);
          break;
        case 1: // Tanh (soft saturation)
          wet = tanhf(sig);
          break;
        case 2: // Softclip
          wet = sig / (1.0f + (sig < 0 ? -sig : sig));
          break;
        case 3: // Hard clip
          wet = sig < -1.0f ? -1.0f : (sig > 1.0f ? 1.0f : sig);
          break;
        case 4: // Sqrt rectify (asymmetric)
          wet = sqrtf(sig < 0 ? -sig : sig) * (sig < 0 ? -1.0f : 1.0f);
          break;
        case 5: // Full-wave rectify
          wet = sig < 0 ? -sig : sig;
          break;
        case 6: // Bitcrush (8 levels)
          wet = ((int)(sig * 8.0f + (sig < 0 ? -0.5f : 0.5f))) / 8.0f;
          break;
        default:
          wet = sig;
          break;
      }

      out[i] = dry + (wet - dry) * mix;
    }
  }

} // namespace stolmine
