#include "FadeMixer.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{

  FadeMixer::FadeMixer()
  {
    addInput(mIn1);
    addInput(mIn2);
    addInput(mIn3);
    addInput(mIn4);
    addInput(mIn5);
    addInput(mIn6);
    addInput(mIn7);
    addInput(mIn8);
    addOutput(mOutput);
    addParameter(mFade);
    addParameter(mLevel);
    addParameter(mInputs);
  }

  FadeMixer::~FadeMixer()
  {
  }

  void FadeMixer::process()
  {
    float *in[8] = {
      mIn1.buffer(), mIn2.buffer(), mIn3.buffer(), mIn4.buffer(),
      mIn5.buffer(), mIn6.buffer(), mIn7.buffer(), mIn8.buffer()};
    float *out = mOutput.buffer();

    float fade = CLAMP(0.0f, 1.0f, mFade.value());
    float level = CLAMP(0.0f, 4.0f, mLevel.value());
    int n = (int)(mInputs.value() + 0.5f);
    if (n < 2) n = 2;
    if (n > 8) n = 8;

    // Fade sweeps a triangular window across the active inputs: fade=0 -> all
    // input 0, fade=1 -> all input n-1, each input centered at its own slot with
    // an equal-power (sqrt) triangular gain. Same law as the original 4-input
    // unit, generalised to n (at n=4 the gains are byte-identical).
    float pos = fade * (float)(n - 1);
    float g[8];
    for (int c = 0; c < n; c++)
    {
      float lin = 1.0f - CLAMP(0.0f, 1.0f, fabsf(pos - (float)c));
      g[c] = sqrtf(lin);
    }

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float acc = 0.0f;
      for (int c = 0; c < n; c++)
        acc += in[c][i] * g[c];
      out[i] = acc * level;
    }
  }

} // namespace stolmine
