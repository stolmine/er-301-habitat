// Mutable Instruments Warps modulator wrapper for ER-301
// Based on code by Émilie Gillet, MIT License

#include "WarpsModulator.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

#include "warps/dsp/modulator.h"
#include "warps/resources.h"

namespace mi
{

  static const size_t kWarpsBlockSize = 96;
  // Max ER-301 frame + headroom for alignment
  static const size_t kMaxFrameSize = 256;

  struct WarpsModulator::Internal
  {
    warps::Modulator modulator;
    warps::ShortFrame input[kWarpsBlockSize];
    warps::ShortFrame output[kWarpsBlockSize];

    // FIFO: input accumulates, output drains in aligned 96-sample blocks
    float carFifo[kMaxFrameSize + kWarpsBlockSize];
    float modFifo[kMaxFrameSize + kWarpsBlockSize];
    float outLFifo[kMaxFrameSize + kWarpsBlockSize];
    float outRFifo[kMaxFrameSize + kWarpsBlockSize];
    int writePos;  // input write position
    int readPos;   // output read position
    int rendered;  // samples available in output FIFO

    // Gain compensation
    float inputRMS;
    float outputRMS;
    float gainComp;

    void Init()
    {
      memset(input, 0, sizeof(input));
      memset(output, 0, sizeof(output));
      memset(carFifo, 0, sizeof(carFifo));
      memset(modFifo, 0, sizeof(modFifo));
      memset(outLFifo, 0, sizeof(outLFifo));
      memset(outRFifo, 0, sizeof(outRFifo));
      writePos = 0;
      readPos = 0;
      rendered = 0;
      inputRMS = 0.0f;
      outputRMS = 0.0f;
      gainComp = 1.0f;
      // Filter bank coefficients are designed for 96kHz sub-rates
      modulator.Init(96000.0f);
    }

    void RenderBlock(int startPos)
    {
      float inSum = 0.0f;
      for (size_t i = 0; i < kWarpsBlockSize; i++)
      {
        float car = carFifo[startPos + i];
        float mod = modFifo[startPos + i];
        inSum += car * car;
        input[i].l = (short)CLAMP(-32768, 32767, (int)(car * 32767.0f));
        input[i].r = (short)CLAMP(-32768, 32767, (int)(mod * 32767.0f));
      }

      modulator.Process(input, output, kWarpsBlockSize);

      float outSum = 0.0f;
      for (size_t i = 0; i < kWarpsBlockSize; i++)
      {
        float raw = output[i].l / 32768.0f;
        outSum += raw * raw;
      }

      float newInputRMS = sqrtf(inSum / kWarpsBlockSize);
      float newOutputRMS = sqrtf(outSum / kWarpsBlockSize);

      const float smooth = 0.05f;
      inputRMS += smooth * (newInputRMS - inputRMS);
      outputRMS += smooth * (newOutputRMS - outputRMS);

      if (outputRMS > 0.001f)
      {
        float target = inputRMS / outputRMS;
        target = CLAMP(0.1f, 4.0f, target);
        gainComp += smooth * (target - gainComp);
      }

      for (size_t i = 0; i < kWarpsBlockSize; i++)
      {
        outLFifo[startPos + i] = (output[i].l / 32768.0f) * gainComp;
        outRFifo[startPos + i] = (output[i].r / 16384.0f) * gainComp;
      }
    }
  };

  WarpsModulator::WarpsModulator()
  {
    addInput(mCarrier);
    addInput(mModulator);
    addOutput(mOut);
    addOutput(mAux);
    addParameter(mAlgorithm);
    addParameter(mTimbre);
    addParameter(mDrive);
    addOption(mEasterEgg);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  WarpsModulator::~WarpsModulator()
  {
    delete mpInternal;
  }

  void WarpsModulator::process()
  {
    Internal &s = *mpInternal;

    float *carrierBuf = mCarrier.buffer();
    float *modulatorBuf = mModulator.buffer();
    float *outBuf = mOut.buffer();
    float *auxBuf = mAux.buffer();

    // Set parameters
    warps::Parameters *p = s.modulator.mutable_parameters();
    p->carrier_shape = 0;
    float drive = CLAMP(0.0f, 1.0f, mDrive.value());
    p->channel_drive[0] = drive;
    p->channel_drive[1] = drive;
    p->modulation_algorithm = CLAMP(0.0f, 1.0f, mAlgorithm.value());
    p->modulation_parameter = CLAMP(0.0f, 1.0f, mTimbre.value());
    p->frequency_shift_pot = 0.5f;
    p->frequency_shift_cv = 0.0f;
    p->phase_shift = 0.0f;
    p->note = 48.0f;

    s.modulator.set_easter_egg(mEasterEgg.value() == 1);

    // Copy input into FIFO
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      s.carFifo[s.writePos + i] = carrierBuf[i];
      s.modFifo[s.writePos + i] = modulatorBuf[i];
    }

    // Render as many 96-sample blocks as we can
    int inputEnd = s.writePos + FRAMELENGTH;
    int renderPos = s.readPos + s.rendered;
    while (renderPos + (int)kWarpsBlockSize <= inputEnd)
    {
      s.RenderBlock(renderPos);
      s.rendered += kWarpsBlockSize;
      renderPos += kWarpsBlockSize;
    }

    // Read output from FIFO
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      if (s.rendered > 0)
      {
        outBuf[i] = s.outLFifo[s.readPos];
        auxBuf[i] = s.outRFifo[s.readPos];
        s.readPos++;
        s.rendered--;
      }
      else
      {
        // Underrun — output last known value
        outBuf[i] = 0.0f;
        auxBuf[i] = 0.0f;
      }
    }

    // Advance write position
    s.writePos = inputEnd;

    // Compact FIFOs when we've consumed enough
    if (s.readPos >= (int)kWarpsBlockSize * 2)
    {
      int unread = s.writePos - s.readPos;
      if (unread > 0)
      {
        memmove(s.carFifo, s.carFifo + s.readPos, unread * sizeof(float));
        memmove(s.modFifo, s.modFifo + s.readPos, unread * sizeof(float));
        memmove(s.outLFifo, s.outLFifo + s.readPos, unread * sizeof(float));
        memmove(s.outRFifo, s.outRFifo + s.readPos, unread * sizeof(float));
      }
      s.writePos -= s.readPos;
      s.readPos = 0;
    }
  }

} // namespace mi
