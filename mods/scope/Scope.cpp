// Scope — inline signal visualization (passthrough)

#include "Scope.h"
#include <od/config.h>
#include <string.h>

namespace scope_unit
{

  Scope::Scope()
  {
    addInput(mInL);
    addInput(mInR);
    addOutput(mOutL);
    addOutput(mOutR);
  }

  Scope::~Scope() {}

  void Scope::process()
  {
    float *inL = mInL.buffer();
    float *inR = mInR.buffer();
    float *outL = mOutL.buffer();
    float *outR = mOutR.buffer();
    memcpy(outL, inL, FRAMELENGTH * sizeof(float));
    memcpy(outR, inR, FRAMELENGTH * sizeof(float));
  }

} // namespace scope_unit
