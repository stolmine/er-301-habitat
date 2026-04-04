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
    addOutput(mOutput);
    addParameter(mFade);
    addParameter(mLevel);
  }

  FadeMixer::~FadeMixer()
  {
  }

  void FadeMixer::process()
  {
    float *in1 = mIn1.buffer();
    float *in2 = mIn2.buffer();
    float *in3 = mIn3.buffer();
    float *in4 = mIn4.buffer();
    float *out = mOutput.buffer();

    float fade = CLAMP(0.0f, 1.0f, mFade.value());
    float level = CLAMP(0.0f, 4.0f, mLevel.value());

    // Map fade 0-1 across 4 inputs with equal-power crossfade
    // fade=0: all in1, fade=0.333: all in2, fade=0.667: all in3, fade=1: all in4
    // Each input has a triangular window centered at its position
    float pos = fade * 3.0f; // 0-3 across 4 inputs

    float g1 = 1.0f - CLAMP(0.0f, 1.0f, fabsf(pos - 0.0f));
    float g2 = 1.0f - CLAMP(0.0f, 1.0f, fabsf(pos - 1.0f));
    float g3 = 1.0f - CLAMP(0.0f, 1.0f, fabsf(pos - 2.0f));
    float g4 = 1.0f - CLAMP(0.0f, 1.0f, fabsf(pos - 3.0f));

    // Equal-power: sqrt of linear gains
    g1 = sqrtf(g1);
    g2 = sqrtf(g2);
    g3 = sqrtf(g3);
    g4 = sqrtf(g4);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      out[i] = (in1[i] * g1 + in2[i] * g2 + in3[i] * g3 + in4[i] * g4) * level;
    }
  }

} // namespace stolmine
