#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>
#include "grids_resources.h"

#define GRIDS_MODE_TRIGGER 1
#define GRIDS_MODE_GATE 2
#define GRIDS_MODE_THROUGH 3

namespace grids
{

  class Grids : public od::Object
  {
  public:
    Grids();
    virtual ~Grids();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mClock{"Clock"};
    od::Inlet mReset{"Reset"};
    od::Outlet mOut{"Out"};

    od::Parameter mMapX{"Map X", 0.5f};
    od::Parameter mMapY{"Map Y", 0.5f};
    od::Parameter mDensity{"Density", 0.5f};
    od::Parameter mChaos{"Chaos", 0.0f};
    od::Parameter mWidth{"Width", 0.5f};

    od::Parameter mChannel{"Channel", 0.0f}; // 0=BD, 1=SD, 2=HH

    od::Option mMode{"Mode", GRIDS_MODE_TRIGGER};
#endif

    int getStep() { return mStep; }
    bool isSet(int i);

  private:
    int mStep = 0;

    bool mClockWasHigh = false;
    bool mResetWasHigh = false;

    int mGateSamplesRemaining = 0;
    int mClockPeriodSamples = 0;
    int mSamplesSinceLastClock = 0;

    // Randomness state
    uint32_t mRandomState = 1;
    uint8_t mPerturbation = 0;

    uint8_t randomByte();

    static uint8_t u8mix(uint8_t a, uint8_t b, uint8_t mix);
    static uint8_t readDrumMap(uint8_t step, uint8_t instrument,
                               uint8_t x, uint8_t y);
  };

} // namespace grids
