// Mutable Instruments Marbles T (probabilistic gate generator) for ER-301
// Based on code by Emilie Gillet, MIT License

#include "MarblesT.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

#include "marbles/random/t_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/random_generator.h"
#include "stmlib/utils/gate_flags.h"

namespace mi
{

  static const int kMaxBlockSize = 128;

  struct MarblesT::Internal
  {
    marbles::TGenerator t_gen;
    marbles::RandomGenerator rng;
    marbles::RandomStream random_stream;

    float ramp_external[kMaxBlockSize];
    float ramp_master[kMaxBlockSize];
    float ramp_slave0[kMaxBlockSize];
    float ramp_slave1[kMaxBlockSize];
    bool gate[2 * kMaxBlockSize];

    stmlib::GateFlags clock_flags[kMaxBlockSize];
    stmlib::GateFlags prev_clock_flag;
    bool resetWasHigh;
    bool clockReceived;
  };

  MarblesT::MarblesT()
  {
    addInput(mClock);
    addInput(mReset);
    addOutput(mOut);
    addParameter(mJitter);
    addParameter(mDejaVu);
    addParameter(mLength);
    addParameter(mOutput);
    addOption(mModel);

    mpInternal = new Internal{};

    mpInternal->rng.Init(0x12345678);
    mpInternal->random_stream.Init(&mpInternal->rng);
    mpInternal->t_gen.Init(&mpInternal->random_stream, globalConfig.sampleRate);
    mpInternal->prev_clock_flag = stmlib::GATE_FLAG_LOW;
    mpInternal->resetWasHigh = false;
    mpInternal->clockReceived = false;
  }

  MarblesT::~MarblesT()
  {
    delete mpInternal;
  }

  void MarblesT::process()
  {
    float *clock = mClock.buffer();
    float *resetIn = mReset.buffer();
    float *out = mOut.buffer();
    Internal &s = *mpInternal;

    // Read parameters
    float jitter = CLAMP(0.0f, 1.0f, mJitter.value());
    float deja_vu = CLAMP(0.0f, 1.0f, mDejaVu.value());
    int length = (int)CLAMP(1.0f, 16.0f, mLength.value());
    float output = CLAMP(0.0f, 1.0f, mOutput.value());
    int model = mModel.value();
    model = CLAMP(0, 6, model);

    // Configure T generator
    s.t_gen.set_model(static_cast<marbles::TGeneratorModel>(model));
    s.t_gen.set_range(marbles::T_GENERATOR_RANGE_1X);
    s.t_gen.set_rate(0.0f);
    s.t_gen.set_bias(output);
    s.t_gen.set_jitter(jitter);
    s.t_gen.set_deja_vu(deja_vu);
    s.t_gen.set_length(length);
    s.t_gen.set_pulse_width_mean(0.5f);
    s.t_gen.set_pulse_width_std(0.0f);

    // Convert clock input to GateFlags
    stmlib::GateFlags prev = s.prev_clock_flag;
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool high = clock[i] > 0.0f;
      stmlib::GateFlags flag;
      if (high)
      {
        if (!(prev & stmlib::GATE_FLAG_HIGH))
          s.clockReceived = true;
        flag = (prev & stmlib::GATE_FLAG_HIGH)
                   ? stmlib::GATE_FLAG_HIGH
                   : static_cast<stmlib::GateFlags>(stmlib::GATE_FLAG_RISING | stmlib::GATE_FLAG_HIGH);
      }
      else
      {
        flag = (prev & stmlib::GATE_FLAG_HIGH)
                   ? stmlib::GATE_FLAG_FALLING
                   : stmlib::GATE_FLAG_LOW;
      }
      s.clock_flags[i] = flag;
      prev = flag;
    }
    s.prev_clock_flag = prev;

    // Output silence until first clock arrives
    if (!s.clockReceived)
    {
      memset(out, 0, FRAMELENGTH * sizeof(float));
      return;
    }

    // Detect reset rising edge
    bool reset = false;
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool high = resetIn[i] > 0.0f;
      if (high && !s.resetWasHigh)
      {
        reset = true;
      }
      s.resetWasHigh = high;
    }

    // Clear ramp buffers
    memset(s.ramp_external, 0, sizeof(s.ramp_external));
    memset(s.ramp_master, 0, sizeof(s.ramp_master));
    memset(s.ramp_slave0, 0, sizeof(s.ramp_slave0));
    memset(s.ramp_slave1, 0, sizeof(s.ramp_slave1));
    memset(s.gate, 0, sizeof(s.gate));

    // Build Ramps struct
    marbles::Ramps ramps;
    ramps.external = s.ramp_external;
    ramps.master = s.ramp_master;
    ramps.slave[0] = s.ramp_slave0;
    ramps.slave[1] = s.ramp_slave1;

    // Process
    s.t_gen.Process(
        true, // use external clock
        &reset,
        s.clock_flags,
        ramps,
        s.gate,
        FRAMELENGTH);

    // Mix T1/T2 to output based on output fader
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float t1 = s.gate[i * 2] ? 1.0f : 0.0f;
      float t2 = s.gate[i * 2 + 1] ? 1.0f : 0.0f;
      out[i] = t1 * (1.0f - output) + t2 * output;
    }
  }

} // namespace mi
