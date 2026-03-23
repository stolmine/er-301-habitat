// Stratos — Clouds reverb engine extracted as standalone ER-301 unit
// Based on code by Émilie Gillet, MIT License

#include "Stratos.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

#include "clouds/dsp/frame.h"
#include "clouds/dsp/fx/reverb.h"

namespace stratos
{

  struct Stratos::Internal
  {
    clouds::Reverb reverb;
    uint16_t buffer[16384];

    void Init()
    {
      memset(buffer, 0, sizeof(buffer));
      reverb.Init(buffer);
    }
  };

  Stratos::Stratos()
  {
    addInput(mInL);
    addInput(mInR);
    addOutput(mOutL);
    addOutput(mOutR);
    addParameter(mAmount);
    addParameter(mTime);
    addParameter(mDiffusion);
    addParameter(mDamping);
    addParameter(mGain);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Stratos::~Stratos()
  {
    delete mpInternal;
  }

  void Stratos::process()
  {
    Internal &s = *mpInternal;

    float *inL = mInL.buffer();
    float *inR = mInR.buffer();
    float *outL = mOutL.buffer();
    float *outR = mOutR.buffer();

    float amount = CLAMP(0.0f, 1.0f, mAmount.value());
    float time = CLAMP(0.0f, 1.0f, mTime.value());
    float diffusion = CLAMP(0.0f, 1.0f, mDiffusion.value());
    float damping = CLAMP(0.0f, 1.0f, mDamping.value());
    float gain = CLAMP(0.0f, 1.0f, mGain.value());

    s.reverb.set_amount(amount);
    s.reverb.set_time(time);
    s.reverb.set_diffusion(diffusion);
    s.reverb.set_lp(damping);
    s.reverb.set_input_gain(gain);

    // Build FloatFrame array from input buffers
    clouds::FloatFrame frames[FRAMELENGTH];
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      frames[i].l = inL[i];
      frames[i].r = inR[i];
    }

    // Process reverb (in-place)
    s.reverb.Process(frames, FRAMELENGTH);

    // Write output
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      outL[i] = frames[i].l;
      outR[i] = frames[i].r;
    }
  }

} // namespace stratos
