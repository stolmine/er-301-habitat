// Mutable Instruments Marbles X (random CV generator) for ER-301
// Based on code by Emilie Gillet, MIT License

#include "MarblesX.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

#include "marbles/random/x_y_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/random_generator.h"
#include "stmlib/utils/gate_flags.h"

namespace mi
{

  static const int kMaxBlockSize = 128;

  struct MarblesX::Internal
  {
    marbles::XYGenerator xy_gen;
    marbles::RandomGenerator rng;
    marbles::RandomStream random_stream;

    float ramp_external[kMaxBlockSize];
    float ramp_master[kMaxBlockSize];
    float ramp_slave0[kMaxBlockSize];
    float ramp_slave1[kMaxBlockSize];
    float output_buffer[4 * kMaxBlockSize];

    stmlib::GateFlags clock_flags[kMaxBlockSize];
    stmlib::GateFlags prev_clock_flag;
    bool resetWasHigh;
  };

  MarblesX::MarblesX()
  {
    addInput(mClock);
    addInput(mReset);
    addOutput(mOut);
    addParameter(mSpread);
    addParameter(mBias);
    addParameter(mSteps);
    addParameter(mDejaVu);
    addParameter(mLength);
    addParameter(mOutput);
    addOption(mControlMode);

    mpInternal = new Internal{};

    mpInternal->rng.Init(0x87654321);
    mpInternal->random_stream.Init(&mpInternal->rng);
    mpInternal->xy_gen.Init(&mpInternal->random_stream, globalConfig.sampleRate);
    mpInternal->prev_clock_flag = stmlib::GATE_FLAG_LOW;
    mpInternal->resetWasHigh = false;
  }

  MarblesX::~MarblesX()
  {
    delete mpInternal;
  }

  void MarblesX::process()
  {
    float *clock = mClock.buffer();
    float *resetIn = mReset.buffer();
    float *out = mOut.buffer();
    Internal &s = *mpInternal;

    // Read parameters
    float spread = CLAMP(0.0f, 1.0f, mSpread.value());
    float bias = CLAMP(0.0f, 1.0f, mBias.value());
    float steps = CLAMP(0.0f, 1.0f, mSteps.value());
    float deja_vu = CLAMP(0.0f, 1.0f, mDejaVu.value());
    int length = (int)CLAMP(1.0f, 16.0f, mLength.value());
    float output = CLAMP(0.0f, 1.0f, mOutput.value());
    int control_mode = mControlMode.value();
    control_mode = CLAMP(0, 2, control_mode);

    // Convert clock input to GateFlags
    stmlib::GateFlags prev = s.prev_clock_flag;
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool high = clock[i] > 0.0f;
      stmlib::GateFlags flag;
      if (high)
      {
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
    memset(s.output_buffer, 0, sizeof(s.output_buffer));

    // Build Ramps struct
    marbles::Ramps ramps;
    ramps.external = s.ramp_external;
    ramps.master = s.ramp_master;
    ramps.slave[0] = s.ramp_slave0;
    ramps.slave[1] = s.ramp_slave1;

    // Build X settings
    marbles::GroupSettings x_settings;
    x_settings.control_mode = static_cast<marbles::ControlMode>(control_mode);
    x_settings.voltage_range = marbles::VOLTAGE_RANGE_FULL;
    x_settings.register_mode = false;
    x_settings.register_value = 0.0f;
    x_settings.spread = spread;
    x_settings.bias = bias;
    x_settings.steps = steps;
    x_settings.deja_vu = deja_vu;
    x_settings.scale_index = 0;
    x_settings.length = length;
    x_settings.ratio.p = 1;
    x_settings.ratio.q = 1;

    // Build Y settings (neutral -- we ignore Y output)
    marbles::GroupSettings y_settings;
    y_settings.control_mode = marbles::CONTROL_MODE_IDENTICAL;
    y_settings.voltage_range = marbles::VOLTAGE_RANGE_FULL;
    y_settings.register_mode = false;
    y_settings.register_value = 0.0f;
    y_settings.spread = 0.5f;
    y_settings.bias = 0.5f;
    y_settings.steps = 0.5f;
    y_settings.deja_vu = 0.0f;
    y_settings.scale_index = 0;
    y_settings.length = 8;
    y_settings.ratio.p = 1;
    y_settings.ratio.q = 1;

    // Process
    s.xy_gen.Process(
        marbles::CLOCK_SOURCE_EXTERNAL,
        x_settings,
        y_settings,
        &reset,
        s.clock_flags,
        ramps,
        s.output_buffer,
        FRAMELENGTH);

    // Select/blend X1/X2/X3 based on output fader
    // output 0.0 = X1, 0.5 = X2, 1.0 = X3
    float pos = output * 2.0f;
    int idx = (int)pos;
    if (idx > 1) idx = 1;
    float frac = pos - (float)idx;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float a = s.output_buffer[i * 4 + idx];
      float b = s.output_buffer[i * 4 + idx + 1];
      // XYGenerator outputs +-5V range; normalize to +-1V for ER-301
      out[i] = (a * (1.0f - frac) + b * frac) * 0.2f;
    }
  }

} // namespace mi
