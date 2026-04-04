// Gesture Sequencer: continuous gesture recorder/looper for ER-301
// Records fader movements into an od::Sample buffer, loops on playback.

#include "GestureSeq.h"
#include <od/config.h>
#include <od/audio/Sample.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

namespace stolmine
{

  // Sensitivity presets: {threshold, holdoff samples}
  static const float kThresholds[] = {0.005f, 0.002f, 0.0005f};
  static const int kHoldoffs[] = {8192, 4096, 1024};

  struct GestureSeq::Internal
  {
    // Movement detection
    float prevOffset;
    bool moving;
    int holdoffCounter;

    // Edge detection
    bool resetWasHigh;

    // Output slew
    float slewedOutput;
  };

  GestureSeq::GestureSeq()
  {
    addInput(mRun);
    addInput(mReset);
    addInput(mErase);
    addOutput(mOut);
    addParameter(mOffset);
    addParameter(mSlew);
    addOption(mWriteActive);
    addOption(mSensitivity);

    mpInternal = new Internal();
    mpInternal->prevOffset = 0.0f;
    mpInternal->moving = false;
    mpInternal->holdoffCounter = 0;
    mpInternal->resetWasHigh = false;
    mpInternal->slewedOutput = 0.0f;
  }

  GestureSeq::~GestureSeq()
  {
    delete mpInternal;
  }

  void GestureSeq::process()
  {
    float *run = mRun.buffer();
    float *reset = mReset.buffer();
    float *erase = mErase.buffer();
    float *out = mOut.buffer();
    Internal &s = *mpInternal;

    if (mpSample == 0 || mEndIndex <= 0)
    {
      memset(out, 0, FRAMELENGTH * sizeof(float));
      return;
    }

    float offset = mOffset.value();
    float slewTime = mSlew.value();

    // Select sensitivity preset
    int sens = CLAMP(0, 2, (int)mSensitivity.value());
    float moveThreshold = kThresholds[sens];
    int holdoffSamples = kHoldoffs[sens];

    // Movement detection (frame-level since offset is a Parameter)
    float delta = fabsf(offset - s.prevOffset);
    if (delta > moveThreshold)
    {
      s.moving = true;
      s.holdoffCounter = holdoffSamples;
    }
    s.prevOffset = offset;

    // Holdoff countdown (frame-level)
    if (s.holdoffCounter > 0)
    {
      s.holdoffCounter -= FRAMELENGTH;
      if (s.holdoffCounter <= 0)
      {
        s.holdoffCounter = 0;
        s.moving = false;
      }
    }

    // Expose write state to UI
    mWriteActive.set(s.moving ? 1 : 0);

    // Slew coefficient
    bool useSlew = slewTime > 0.001f;
    float slewCoeff = 0.0f;
    if (useSlew)
    {
      slewCoeff = 1.0f - expf(-1.0f / (slewTime * globalConfig.sampleRate));
    }

    bool dirty = false;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool runHigh = run[i] > 0.0f;
      bool resetHigh = reset[i] > 0.0f;
      bool resetRise = resetHigh && !s.resetWasHigh;
      s.resetWasHigh = resetHigh;

      if (resetRise)
      {
        mCurrentIndex = 0;
      }

      if (runHigh)
      {
        // Write > erase > playback
        if (s.moving)
        {
          mpSample->set(mCurrentIndex, 0, offset);
          dirty = true;
        }
        else if (erase[i] > 0.0f)
        {
          mpSample->set(mCurrentIndex, 0, 0.0f);
          dirty = true;
        }

        // Read from buffer
        float raw = mpSample->get(mCurrentIndex, 0);

        // Apply output slew
        if (useSlew)
        {
          s.slewedOutput += (raw - s.slewedOutput) * slewCoeff;
          out[i] = s.slewedOutput;
        }
        else
        {
          s.slewedOutput = raw;
          out[i] = raw;
        }

        // Advance head
        mCurrentIndex = (mCurrentIndex + 1) % mEndIndex;
      }
      else
      {
        out[i] = 0.0f;
      }
    }

    if (dirty)
    {
      mpSample->setDirty();
    }
  }

} // namespace stolmine
