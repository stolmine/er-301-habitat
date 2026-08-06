#include "FadeMixer.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{

  // Declick ramp for Snap mode: long enough to kill the step, short enough that
  // the switch still reads as instant.
  static const float kDeclickSec = 0.003f;

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
    addOption(mMode);
    mMode.enableSerialization();
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

    bool snap = mMode.value() == 2;

    // Target gains, computed once per block.
    //
    // SMOOTH: fade sweeps a triangular window across the active inputs. fade=0
    // -> all input 0, fade=1 -> all input n-1, each input centered at its own
    // slot with an equal-power (sqrt) triangular gain. Same law as the original
    // 4-input unit, generalised to n (at n=4 the gains are byte-identical).
    //
    // SNAP: the unit becomes an N->1 switch - the input the smooth mode would be
    // LOUDEST on takes the whole output and the rest go silent. Selecting by
    // nearest centre (rather than by equal 1/n slices) is what keeps the two
    // modes aligned: toggling the option never jumps to a different input, and a
    // given fade position means the same thing either way. The consequence is
    // that the two END inputs own half-width zones, since their centres sit on
    // the ends of the throw.
    float pos = fade * (float)(n - 1);
    float tgt[8];
    if (snap)
    {
      int sel = (int)(pos + 0.5f);
      sel = CLAMP(0, n - 1, sel);
      for (int c = 0; c < n; c++)
        tgt[c] = (c == sel) ? 1.0f : 0.0f;
    }
    else
    {
      for (int c = 0; c < n; c++)
      {
        float lin = 1.0f - CLAMP(0.0f, 1.0f, fabsf(pos - (float)c));
        tgt[c] = sqrtf(lin);
      }
    }

    // Declick. A hard switch mid-waveform steps the output and clicks, so the
    // live gains chase their targets through a one-pole. The coefficient is
    // picked at BLOCK rate and the per-sample path is identical in both modes -
    // no runtime branch inside the loop, per
    // feedback_runtime_branched_dsp_dispatch. At coeff 1.0 the gain equals the
    // target on the first sample, so SMOOTH stays bit-identical to before.
    float coeff = snap ? (1.0f - expf(-1.0f / (kDeclickSec * globalConfig.sampleRate))) : 1.0f;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float acc = 0.0f;
      for (int c = 0; c < n; c++)
      {
        mG[c] += coeff * (tgt[c] - mG[c]);
        acc += in[c][i] * mG[c];
      }
      out[i] = acc * level;
    }
  }

} // namespace stolmine
