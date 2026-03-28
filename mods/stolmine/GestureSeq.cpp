// Gesture Sequencer: continuous gesture recorder/looper for ER-301
// Records fader movements into a sample-rate buffer, loops on playback.

#include "GestureSeq.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

namespace stolmine
{

  static const int kBufferSizes[] = {5, 10, 20};
  static const int kNumBufferSizes = 3;

  // Movement detection tuning
  static const float kMoveThreshold = 0.001f;
  static const int kHoldoffSamples = 2048; // ~43ms at 48kHz

  struct GestureSeq::Internal
  {
    float *buffer;
    int bufferLength;
    int allocatedSeconds;

    int position;

    // Movement detection
    float prevOffset;
    bool moving;
    int holdoffCounter;

    // Edge detection
    bool resetWasHigh;

    // Pending realloc
    int pendingSeconds;
  };

  GestureSeq::GestureSeq()
  {
    addInput(mRun);
    addInput(mReset);
    addInput(mWrite);
    addOutput(mOut);
    addParameter(mOffset);
    addOption(mBufferSize);

    mpInternal = new Internal();
    mpInternal->buffer = nullptr;
    mpInternal->bufferLength = 0;
    mpInternal->allocatedSeconds = 0;
    mpInternal->position = 0;
    mpInternal->prevOffset = 0.0f;
    mpInternal->moving = false;
    mpInternal->holdoffCounter = 0;
    mpInternal->resetWasHigh = false;
    mpInternal->pendingSeconds = 0;

    // Allocate default buffer (5 seconds, index 0)
    int seconds = kBufferSizes[0];
    mpInternal->bufferLength = seconds * (int)globalConfig.sampleRate;
    mpInternal->buffer = new float[mpInternal->bufferLength]();
    mpInternal->allocatedSeconds = seconds;
  }

  GestureSeq::~GestureSeq()
  {
    if (mpInternal->buffer)
    {
      delete[] mpInternal->buffer;
    }
    delete mpInternal;
  }

  void GestureSeq::clear()
  {
    if (mpInternal->buffer)
    {
      memset(mpInternal->buffer, 0, mpInternal->bufferLength * sizeof(float));
    }
    mpInternal->position = 0;
  }

  int GestureSeq::getBufferSeconds()
  {
    return mpInternal->allocatedSeconds;
  }

  void GestureSeq::process()
  {
    float *run = mRun.buffer();
    float *reset = mReset.buffer();
    float *write = mWrite.buffer();
    float *out = mOut.buffer();
    Internal &s = *mpInternal;

    // Check for buffer size change -- defer realloc until run is low
    int idx = CLAMP(0, kNumBufferSizes - 1, (int)mBufferSize.value());
    int seconds = kBufferSizes[idx];
    if (seconds != s.allocatedSeconds)
    {
      s.pendingSeconds = seconds;
    }

    // Only reallocate when run gate is low (silent) to avoid glitches
    if (s.pendingSeconds > 0 && run[0] <= 0.0f)
    {
      delete[] s.buffer;
      s.bufferLength = s.pendingSeconds * (int)globalConfig.sampleRate;
      s.buffer = new float[s.bufferLength]();
      s.allocatedSeconds = s.pendingSeconds;
      s.pendingSeconds = 0;
      s.position = 0;
    }

    float offset = mOffset.value();
    bool writeConnected = mWrite.isConnected();

    // Movement detection (frame-level since offset is a Parameter)
    float delta = fabsf(offset - s.prevOffset);
    if (delta > kMoveThreshold)
    {
      s.moving = true;
      s.holdoffCounter = kHoldoffSamples;
    }
    s.prevOffset = offset;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Edge detection
      bool runHigh = run[i] > 0.0f;
      bool resetHigh = reset[i] > 0.0f;
      bool resetRise = resetHigh && !s.resetWasHigh;
      s.resetWasHigh = resetHigh;

      // Reset
      if (resetRise)
      {
        s.position = 0;
      }

      if (runHigh)
      {
        // Determine write state
        bool writeActive;
        if (writeConnected)
        {
          writeActive = write[i] > 0.0f;
        }
        else
        {
          writeActive = s.moving;
        }

        // Write to buffer
        if (writeActive)
        {
          if (s.moving)
          {
            s.buffer[s.position] = offset;
          }
          else
          {
            s.buffer[s.position] = 0.0f;
          }
        }

        // Output current buffer position
        out[i] = s.buffer[s.position];

        // Advance head
        s.position = (s.position + 1) % s.bufferLength;
      }
      else
      {
        out[i] = 0.0f;
      }
    }

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
  }

} // namespace stolmine
