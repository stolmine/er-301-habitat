// Mutable Instruments Rings resonator wrapper for ER-301
// Based on code by Émilie Gillet, MIT License

#include "RingsVoice.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/dsp.h"
#include "rings/resources.h"

namespace mi
{

  struct RingsVoice::Internal
  {
    rings::Part part;
    rings::StringSynthPart string_synth;
    rings::Patch patch;
    rings::PerformanceState performance_state;
    uint16_t reverb_buffer[32768]; // 64KB for reverb FxEngine
    bool prev_strum;
    int last_model;
    int last_polyphony;

    void Init()
    {
      memset(reverb_buffer, 0, sizeof(reverb_buffer));
      memset(&patch, 0, sizeof(patch));
      memset(&performance_state, 0, sizeof(performance_state));
      part.Init(reverb_buffer);
      string_synth.Init(reverb_buffer);
      prev_strum = false;
      last_model = -1;
      last_polyphony = -1;

      patch.structure = 0.5f;
      patch.brightness = 0.5f;
      patch.damping = 0.5f;
      patch.position = 0.5f;

      performance_state.tonic = 48.0f;
      performance_state.fm = 0.0f;
      performance_state.chord = 0;
    }
  };

  RingsVoice::RingsVoice()
  {
    addInput(mInput);
    addInput(mVOct);
    addInput(mStrum);
    addOutput(mOut);
    addOutput(mAux);
    addParameter(mStructure);
    addParameter(mBrightness);
    addParameter(mDamping);
    addParameter(mPosition);
    addParameter(mModel);
    addParameter(mFreq);
    addOption(mPolyphony);
    addOption(mEasterEgg);
    addOption(mInternalExciter);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  RingsVoice::~RingsVoice()
  {
    delete mpInternal;
  }

  void RingsVoice::process()
  {
    Internal &s = *mpInternal;

    float *inBuf = mInput.buffer();
    float *voctBuf = mVOct.buffer();
    float *strumBuf = mStrum.buffer();
    float *outBuf = mOut.buffer();
    float *auxBuf = mAux.buffer();

    // Set patch parameters
    s.patch.structure = CLAMP(0.0f, 0.9995f, mStructure.value());
    s.patch.brightness = CLAMP(0.0f, 0.9995f, mBrightness.value());
    s.patch.damping = CLAMP(0.0f, 0.9995f, mDamping.value());
    s.patch.position = CLAMP(0.0f, 0.9995f, mPosition.value());

    // Model and polyphony
    int model = (int)CLAMP(0, 5, (int)mModel.value());
    int polyOpt = mPolyphony.value();
    int polyphony = (polyOpt == 0) ? 1 : (polyOpt == 1) ? 2 : 4;

    if (model != s.last_model)
    {
      s.part.set_model(static_cast<rings::ResonatorModel>(model));
      s.last_model = model;
    }
    if (polyphony != s.last_polyphony)
    {
      s.part.set_polyphony(polyphony);
      s.last_polyphony = polyphony;
    }

    // Performance state
    s.performance_state.internal_exciter = mInternalExciter.value() == 1;
    s.performance_state.internal_strum = false;
    s.performance_state.internal_note = false;

    bool easterEgg = mEasterEgg.value() == 1;

    // Process in 24-sample chunks
    const int blockSize = rings::kMaxBlockSize;
    int pos = 0;

    while (pos < FRAMELENGTH)
    {
      int remaining = FRAMELENGTH - pos;
      int chunk = (remaining >= blockSize) ? blockSize : remaining;

      // Pitch: base note + freq offset + V/Oct
      float freqOffset = mFreq.value();
      s.performance_state.tonic = 48.0f + freqOffset;
      s.performance_state.note = voctBuf[pos] * 12.0f;
      s.performance_state.fm = 0.0f;

      // Strum: edge detect from gate
      bool gate = strumBuf[pos] > 0.5f;
      s.performance_state.strum = gate && !s.prev_strum;
      s.prev_strum = gate;

      // Process
      if (easterEgg)
      {
        s.string_synth.Process(
            s.performance_state, s.patch,
            inBuf + pos, outBuf + pos, auxBuf + pos, chunk);
      }
      else
      {
        s.part.Process(
            s.performance_state, s.patch,
            inBuf + pos, outBuf + pos, auxBuf + pos, chunk);
      }

      pos += chunk;
    }
  }

} // namespace mi
