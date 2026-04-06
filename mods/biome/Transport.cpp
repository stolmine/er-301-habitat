#include "Transport.h"
#include <od/config.h>
#include <hal/ops.h>

namespace stolmine
{

  Transport::Transport()
  {
    addInput(mRunGate);
    addOutput(mOutput);
    addParameter(mRate);
  }

  Transport::~Transport() {}

  void Transport::process()
  {
    float *run = mRunGate.buffer();
    float *out = mOutput.buffer();

    // Rate parameter is in BPM, convert to Hz at 4 ppqn (16th notes)
    float bpm = CLAMP(1.0f, 300.0f, mRate.value());
    float rate = bpm * 4.0f / 60.0f;
    float dt = globalConfig.samplePeriod;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Comparator in toggle mode: high = running, low = stopped
      bool running = run[i] > 0.5f;

      // Reset phase on start and stop edges
      if (running != mRunning)
      {
        mPhase = 0.0f;
        mRunning = running;
      }

      if (mRunning)
      {
        // Output high for first half of cycle, low for second
        out[i] = (mPhase < 0.5f) ? 1.0f : 0.0f;

        mPhase += rate * dt;
        if (mPhase >= 1.0f)
          mPhase -= 1.0f;
      }
      else
      {
        out[i] = 0.0f;
      }
    }
  }

} // namespace stolmine
