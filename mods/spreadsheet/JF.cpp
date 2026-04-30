// JF — hex-voiced harmonically-coupled slope-engine voice. v1.
// See planning/just-friends.md + planning/jf-initial-pass.md.
//
// Phase 1: skeleton. Writes silence to all 7 sub-outs to validate
// the multi-output declaration end-to-end before wiring any DSP.

#include "JF.h"

#include <od/AudioThread.h>
#include <od/config.h>
#include <string.h>

namespace stolmine
{

  JF::JF()
  {
    addInput(mVOct);
    addInput(mFM);
    addOutput(mMix);
    addOutput(mOut1N);
    addOutput(mOut2N);
    addOutput(mOut3N);
    addOutput(mOut4N);
    addOutput(mOut5N);
    addOutput(mOut6N);
    addParameter(mTimeBias);
    addParameter(mIntone);
    addParameter(mRamp);
    addParameter(mCurve);
    addParameter(mFmDepth);
    addParameter(mOut);
    addOption(mRange);
    addOption(mMode);
    addOption(mOutMode);
    mRange.enableSerialization();
    mMode.enableSerialization();
    mOutMode.enableSerialization();
  }

  JF::~JF()
  {
  }

  void JF::process()
  {
    const int frames = FRAMELENGTH;
    const size_t bytes = frames * sizeof(float);

    // Phase 1: silence on every sub-out. Validates outlet declaration
    // and Lua connect() round-trip without DSP risk.
    memset(mMix.buffer(),   0, bytes);
    memset(mOut1N.buffer(), 0, bytes);
    memset(mOut2N.buffer(), 0, bytes);
    memset(mOut3N.buffer(), 0, bytes);
    memset(mOut4N.buffer(), 0, bytes);
    memset(mOut5N.buffer(), 0, bytes);
    memset(mOut6N.buffer(), 0, bytes);
  }

} // namespace stolmine
