// Commotio — Elements exciter section as standalone ER-301 unit
// Based on code by Émilie Gillet, MIT License

#include "Commotio.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

#include "elements/dsp/exciter.h"
#include "elements/dsp/tube.h"
#include "elements/dsp/multistage_envelope.h"
#include "elements/dsp/patch.h"
#include "elements/dsp/dsp.h"
#include "elements/resources.h"

namespace commotio_unit
{

  struct Commotio::Internal
  {
    elements::MultistageEnvelope envelope;
    elements::Exciter bow;
    elements::Exciter blow;
    elements::Exciter strike;
    elements::Tube tube;

    float bow_buffer[elements::kMaxBlockSize];
    float blow_buffer[elements::kMaxBlockSize];
    float strike_buffer[elements::kMaxBlockSize];

    float envelope_value;
    float strength;
    bool previous_gate;

    void Init()
    {
      memset(this, 0, sizeof(Internal));
      envelope.Init();
      bow.Init();
      blow.Init();
      strike.Init();
      tube.Init();

      bow.set_model(elements::EXCITER_MODEL_FLOW);
      bow.set_parameter(0.7f);
      bow.set_timbre(0.5f);
      blow.set_model(elements::EXCITER_MODEL_GRANULAR_SAMPLE_PLAYER);

      envelope.set_adsr(0.5f, 0.5f, 0.5f, 0.5f);
      envelope_value = 0.0f;
      strength = 0.0f;
      previous_gate = false;
    }
  };

  Commotio::Commotio()
  {
    addInput(mIn);
    addInput(mGate);
    addOutput(mOut);
    addParameter(mBowLevel);
    addParameter(mBowTimbre);
    addParameter(mBlowLevel);
    addParameter(mBlowTimbre);
    addParameter(mBlowMeta);
    addParameter(mStrikeLevel);
    addParameter(mStrikeTimbre);
    addParameter(mStrikeMeta);
    addParameter(mEnvShape);
    addParameter(mDamping);
    addParameter(mBrightness);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Commotio::~Commotio()
  {
    delete mpInternal;
  }

  void Commotio::process()
  {
    Internal &s = *mpInternal;

    float *gate = mGate.buffer();
    float *externalIn = mIn.buffer();
    float *out = mOut.buffer();

    float bow_level = CLAMP(0.0f, 1.0f, mBowLevel.value());
    float bow_timbre = CLAMP(0.0f, 1.0f, mBowTimbre.value());
    float blow_level_param = CLAMP(0.0f, 1.0f, mBlowLevel.value());
    float blow_timbre = CLAMP(0.0f, 1.0f, mBlowTimbre.value());
    float blow_meta = CLAMP(0.0f, 1.0f, mBlowMeta.value());
    float strike_level_param = CLAMP(0.0f, 1.0f, mStrikeLevel.value());
    float strike_timbre = CLAMP(0.0f, 1.0f, mStrikeTimbre.value());
    float strike_meta = CLAMP(0.0f, 1.0f, mStrikeMeta.value());
    float env_shape = CLAMP(0.0f, 1.0f, mEnvShape.value());
    float damping = CLAMP(0.0f, 1.0f, mDamping.value());
    float brightness = CLAMP(0.0f, 1.0f, mBrightness.value());

    const int blockSize = elements::kMaxBlockSize;
    int pos = 0;

    while (pos < FRAMELENGTH)
    {
      int remaining = FRAMELENGTH - pos;
      int chunk = (remaining >= blockSize) ? blockSize : remaining;

      // Gate detection (sample at block boundary)
      bool gate_in = gate[pos] > 0.1f;
      uint8_t flags = 0;
      if (gate_in) {
        if (!s.previous_gate) flags |= elements::EXCITER_FLAG_RISING_EDGE;
        flags |= elements::EXCITER_FLAG_GATE;
      } else if (s.previous_gate) {
        flags = elements::EXCITER_FLAG_FALLING_EDGE;
      }
      s.previous_gate = gate_in;

      // Configure envelope — damping scales decay/release times
      float damp_scale = 0.2f + 1.6f * damping;  // 0.2x to 1.8x
      if (env_shape < 0.4f) {
        float a = env_shape * 0.75f + 0.15f;
        float dr = a * 1.8f * damp_scale;
        s.envelope.set_adsr(a, dr, 0.0f, dr);
      } else if (env_shape < 0.6f) {
        float sus = (env_shape - 0.4f) * 5.0f;
        s.envelope.set_adsr(0.45f, 0.81f * damp_scale, sus, 0.81f * damp_scale);
      } else {
        float a = (1.0f - env_shape) * 0.75f + 0.15f;
        float dr = a * 1.8f * damp_scale;
        s.envelope.set_adsr(a, dr, 1.0f, dr);
      }

      float envelope_gain = 1.0f;
      if (env_shape < 0.4f) {
        envelope_gain = 5.0f - env_shape * 10.0f;
      }

      float env_val = s.envelope.Process(flags) * envelope_gain;
      float envelope_increment = (env_val - s.envelope_value) / chunk;

      // Configure exciters — brightness modulates all timbres
      float brightness_factor = 0.4f + 0.6f * brightness;
      s.bow.set_timbre(bow_timbre * brightness_factor);

      s.blow.set_parameter(blow_meta);
      s.blow.set_timbre(blow_timbre * brightness_factor);

      s.strike.set_meta(
          strike_meta <= 0.4f ? strike_meta * 0.625f : strike_meta * 1.25f - 0.25f,
          elements::EXCITER_MODEL_SAMPLE_PLAYER,
          elements::EXCITER_MODEL_PARTICLES);
      s.strike.set_timbre(strike_timbre * brightness_factor);

      // Process exciters
      s.bow.Process(flags, s.bow_buffer, chunk);

      float blow_level = blow_level_param * 1.5f;
      float tube_level = blow_level > 1.0f ? (blow_level - 1.0f) * 2.0f : 0.0f;
      float blow_mix = blow_level < 1.0f ? blow_level * 0.4f : 0.4f;
      s.blow.Process(flags, s.blow_buffer, chunk);

      // Tube processing on blow signal
      float frequency = 261.63f / elements::kSampleRate;  // Middle C normalized
      s.tube.Process(
          frequency,
          env_val,
          damping,
          tube_level,
          s.blow_buffer,
          tube_level * 0.5f,
          chunk);

      s.strike.Process(flags, s.strike_buffer, chunk);

      // Strike level with bleed-through
      float strike_level = strike_level_param * 1.25f;
      float strike_bleed = strike_level > 1.0f ? (strike_level - 1.0f) * 2.0f : 0.0f;
      strike_level = strike_level < 1.0f ? strike_level : 1.0f;
      strike_level *= 1.5f;

      // Sum exciter outputs (from Voice::Process lines 173-193)
      for (int i = 0; i < chunk; i++)
      {
        s.envelope_value += envelope_increment;
        float e = s.envelope_value;

        float sample = 0.0f;
        sample += s.bow_buffer[i] * e * bow_level * 0.125f;
        sample += s.blow_buffer[i] * blow_mix * e;
        sample += s.strike_buffer[i] * strike_level;
        sample += strike_bleed * s.strike_buffer[i];
        sample += externalIn[pos + i];

        out[pos + i] = sample * 0.5f;
      }

      pos += chunk;
    }
  }

} // namespace commotio_unit
