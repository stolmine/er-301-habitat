#include "Gridlock.h"
#include <od/config.h>

namespace stolmine
{

  Gridlock::Gridlock()
  {
    addInput(mGate1);
    addInput(mGate2);
    addInput(mGate3);
    addOutput(mOutput);
    addParameter(mValue1);
    addParameter(mValue2);
    addParameter(mValue3);
  }

  Gridlock::~Gridlock()
  {
  }

  void Gridlock::process()
  {
    float *g1 = mGate1.buffer();
    float *g2 = mGate2.buffer();
    float *g3 = mGate3.buffer();
    float *out = mOutput.buffer();

    float v1 = mValue1.value();
    float v2 = mValue2.value();
    float v3 = mValue3.value();

    // Priority: gate1 > gate2 > gate3. Output latches last active value.
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      if (g1[i] > 0.5f)
        mHeldValue = v1;
      else if (g2[i] > 0.5f)
        mHeldValue = v2;
      else if (g3[i] > 0.5f)
        mHeldValue = v3;
      out[i] = mHeldValue;
    }
  }

} // namespace stolmine
