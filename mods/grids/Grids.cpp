#include "Grids.h"
#include <od/config.h>
#include <hal/ops.h>

namespace grids
{

  Grids::Grids()
  {
    addInput(mClock);
    addInput(mReset);
    addOutput(mOut);
    addParameter(mMapX);
    addParameter(mMapY);
    addParameter(mDensity);
    addParameter(mChaos);
    addParameter(mWidth);
    addParameter(mChannel);
  }

  Grids::~Grids() {}

  uint8_t Grids::randomByte()
  {
    mRandomState = mRandomState * 1664525 + 1013904223;
    return (uint8_t)(mRandomState >> 24);
  }

  uint8_t Grids::u8mix(uint8_t a, uint8_t b, uint8_t mix)
  {
    return a + (((b - a) * mix) >> 8);
  }

  uint8_t Grids::readDrumMap(uint8_t step, uint8_t instrument,
                              uint8_t x, uint8_t y)
  {
    uint8_t i = x >> 6;
    uint8_t j = y >> 6;
    const uint8_t *a_map = drum_map[i][j];
    const uint8_t *b_map = drum_map[i + 1][j];
    const uint8_t *c_map = drum_map[i][j + 1];
    const uint8_t *d_map = drum_map[i + 1][j + 1];
    uint8_t offset = (instrument * kStepsPerPattern) + step;
    uint8_t a = a_map[offset];
    uint8_t b = b_map[offset];
    uint8_t c = c_map[offset];
    uint8_t d = d_map[offset];
    return u8mix(u8mix(a, b, x << 2), u8mix(c, d, x << 2), y << 2);
  }

  bool Grids::isSet(int i)
  {
    uint8_t x = (uint8_t)CLAMP(0, 255, (int)(mMapX.value() * 255.0f));
    uint8_t y = (uint8_t)CLAMP(0, 255, (int)(mMapY.value() * 255.0f));
    uint8_t density = (uint8_t)CLAMP(0, 255, (int)(mDensity.value() * 255.0f));
    int channel = (int)CLAMP(0, 2, mChannel.value());
    uint8_t step = (uint8_t)(((i % kStepsPerPattern) + kStepsPerPattern) % kStepsPerPattern);
    uint8_t level = readDrumMap(step, channel, x, y);
    uint8_t threshold = ~density;
    return level > threshold;
  }

  void Grids::process()
  {
    float *clock = mClock.buffer();
    float *reset = mReset.buffer();
    float *out = mOut.buffer();

    uint8_t x = (uint8_t)CLAMP(0, 255, (int)(mMapX.value() * 255.0f));
    uint8_t y = (uint8_t)CLAMP(0, 255, (int)(mMapY.value() * 255.0f));
    uint8_t density = (uint8_t)CLAMP(0, 255, (int)(mDensity.value() * 255.0f));
    uint8_t chaos = (uint8_t)CLAMP(0, 255, (int)(mChaos.value() * 255.0f));
    float width = CLAMP(0.0f, 1.0f, mWidth.value());
    int channel = (int)CLAMP(0, 2, mChannel.value());
    uint8_t threshold = ~density;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool clockHigh = clock[i] > 0.0f;
      bool resetHigh = reset[i] > 0.0f;
      bool clockRise = clockHigh && !mClockWasHigh;
      bool resetRise = resetHigh && !mResetWasHigh;

      mClockWasHigh = clockHigh;
      mResetWasHigh = resetHigh;

      if (resetRise)
      {
        mStep = 0;
        mGateSamplesRemaining = 0;
      }

      if (clockRise)
      {
        // Measure clock period
        if (mSamplesSinceLastClock > 0)
        {
          mClockPeriodSamples = mSamplesSinceLastClock;
        }
        mSamplesSinceLastClock = 0;

        // Recalculate perturbation at pattern start
        if (mStep == 0)
        {
          uint8_t chaosScaled = chaos >> 2;
          mPerturbation = (uint8_t)((randomByte() * chaosScaled) >> 8);
        }

        // Evaluate current step
        uint8_t level = readDrumMap(mStep, channel, x, y);
        // Add perturbation with saturation
        uint16_t perturbed = (uint16_t)level + mPerturbation;
        if (perturbed > 255) perturbed = 255;
        bool active = (uint8_t)perturbed > threshold;

        if (active)
        {
          int gateSamples;
          if (mClockPeriodSamples > 0)
          {
            gateSamples = (int)(width * mClockPeriodSamples);
          }
          else
          {
            gateSamples = 48;
          }
          if (gateSamples < 1)
            gateSamples = 1;
          mGateSamplesRemaining = gateSamples;
        }

        // Advance step
        mStep = (mStep + 1) % kStepsPerPattern;
      }

      mSamplesSinceLastClock++;

      // Output
      float value = 0.0f;
      if (mGateSamplesRemaining > 0)
      {
        value = 1.0f;
        mGateSamplesRemaining--;
      }

      out[i] = value;
    }
  }

} // namespace grids
