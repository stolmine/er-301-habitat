// house::Smoketest
//
// Phase 0 of the Cabinet port: stand up a CabinetDSP instance and
// run it for ~50 audio blocks on the actual hardware, logging
// entry / exit / pass / fail. The goal is to catch a CloudSeed-
// style first-frame Cortex-A8 hang BEFORE we wrap CabinetDSP in
// a real Cabinet unit. If the device hangs after "pre-process"
// and never reaches "post-process", we know the DSP body itself
// is the problem and can bisect inside CabinetDSP::process().
//
// On all subsequent calls (after the test completes), Smoketest
// behaves as a transparent passthrough.
//
// Audio thread allocates the CabinetDSP on the heap to avoid
// the small audio-thread stack (~113 KB of state arrays would
// blow it).

#pragma once

#include "CabinetDSP.h"
#include <od/AudioThread.h>
#include <od/config.h>
#include <od/objects/Object.h>
#include <hal/log.h>
#include <cmath>
#include <string.h>

namespace house
{

  // Number of audio blocks to drive into the DSP before declaring
  // a pass. 50 blocks at FRAMELENGTH=256 / 48 kHz = ~267 ms of
  // audio; long enough to exercise the outer Bezier cycle at
  // default derez and to fill the longest 6x6 line a few times.
  static const int kSmoketestBlocks = 50;

  class Smoketest : public od::Object
  {
  public:
    Smoketest()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
    }

    virtual ~Smoketest()
    {
      if (mpDSP) delete mpDSP;
    }

#ifndef SWIGLUA
    od::Inlet  mInL{"In L"};
    od::Inlet  mInR{"In R"};
    od::Outlet mOutL{"Out L"};
    od::Outlet mOutR{"Out R"};

    virtual void process()
    {
      float *inL  = mInL.buffer();
      float *inR  = mInR.buffer();
      float *outL = mOutL.buffer();
      float *outR = mOutR.buffer();

      if (mState == State::Idle)
      {
        logInfo("[Smoketest] pre-construct");
        mpDSP = new CabinetDSP();
        logInfo("[Smoketest] post-construct (state %d KB)", (int)(sizeof(CabinetDSP) / 1024));
        mState = State::Running;
        mBlocksRun = 0;
        mAnyNonZero = false;
        mAnyNonFinite = false;
      }

      if (mState == State::Running)
      {
        logInfo("[Smoketest] pre-process block %d / %d", mBlocksRun + 1, kSmoketestBlocks);
        mpDSP->process(inL, inR, outL, outR, FRAMELENGTH,
                       (float)globalConfig.sampleRate);
        logInfo("[Smoketest] post-process block %d", mBlocksRun + 1);

        // Sanity check output of this block.
        for (int i = 0; i < FRAMELENGTH; i++)
        {
          float v = outL[i];
          if (!std::isfinite(v)) mAnyNonFinite = true;
          if (v != 0.0f) mAnyNonZero = true;
          v = outR[i];
          if (!std::isfinite(v)) mAnyNonFinite = true;
          if (v != 0.0f) mAnyNonZero = true;
        }

        mBlocksRun++;
        if (mBlocksRun >= kSmoketestBlocks)
        {
          if (mAnyNonFinite)
          {
            logError("[Smoketest] FAIL: non-finite output detected");
          }
          else if (!mAnyNonZero)
          {
            logError("[Smoketest] FAIL: all output samples were zero");
          }
          else
          {
            logInfo("[Smoketest] PASS: %d blocks, output finite + non-zero",
                    kSmoketestBlocks);
          }
          delete mpDSP;
          mpDSP = nullptr;
          mState = State::Done;
        }
        return; // outputs already written by DSP
      }

      // Done: transparent passthrough.
      memcpy(outL, inL, FRAMELENGTH * sizeof(float));
      memcpy(outR, inR, FRAMELENGTH * sizeof(float));
    }

  private:
    enum class State : int { Idle = 0, Running = 1, Done = 2 };
    State mState = State::Idle;
    CabinetDSP *mpDSP = nullptr;
    int mBlocksRun = 0;
    bool mAnyNonZero = false;
    bool mAnyNonFinite = false;
#endif
  };

} // namespace house
