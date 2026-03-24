#pragma once

#include <od/objects/Object.h>

namespace peaks_unit
{

// Standard unit: single gate input
#define PEAKS_UNIT_MEMBERS                                                   \
    virtual void process();                                                  \
    od::Inlet mGate{"Gate"};                                                 \
    od::Outlet mOut{"Out"};                                                  \
    od::Parameter mParam1{"Param1", 0.5f};                                   \
    od::Parameter mParam2{"Param2", 0.5f};                                   \
    od::Parameter mParam3{"Param3", 0.5f};                                   \
    od::Parameter mParam4{"Param4", 0.5f};

// Units with separate clock and reset inputs (synced LFOs, sequencers)
#define PEAKS_UNIT_MEMBERS_WITH_RESET                                        \
    virtual void process();                                                  \
    od::Inlet mClock{"Clock"};                                               \
    od::Inlet mReset{"Reset"};                                               \
    od::Outlet mOut{"Out"};                                                  \
    od::Parameter mParam1{"Param1", 0.5f};                                   \
    od::Parameter mParam2{"Param2", 0.5f};                                   \
    od::Parameter mParam3{"Param3", 0.5f};                                   \
    od::Parameter mParam4{"Param4", 0.5f};

// Free-running units with optional reset input only (no clock sync)
#define PEAKS_UNIT_MEMBERS_RESET_ONLY                                        \
    virtual void process();                                                  \
    od::Inlet mReset{"Reset"};                                               \
    od::Outlet mOut{"Out"};                                                  \
    od::Parameter mParam1{"Param1", 0.5f};                                   \
    od::Parameter mParam2{"Param2", 0.5f};                                   \
    od::Parameter mParam3{"Param3", 0.5f};                                   \
    od::Parameter mParam4{"Param4", 0.5f};

  // Units with clock + reset
  class TapLfo : public od::Object {
  public: TapLfo(); virtual ~TapLfo();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS_WITH_RESET
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class FmLfo : public od::Object {
  public: FmLfo(); virtual ~FmLfo();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS_RESET_ONLY
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class WsmLfo : public od::Object {
  public: WsmLfo(); virtual ~WsmLfo();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS_RESET_ONLY
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class Plo : public od::Object {
  public: Plo(); virtual ~Plo();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS_WITH_RESET
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class MiniSequencer : public od::Object {
  public: MiniSequencer(); virtual ~MiniSequencer();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS_WITH_RESET
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class ModSequencer : public od::Object {
  public: ModSequencer(); virtual ~ModSequencer();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS_WITH_RESET
#endif
  private: struct Internal; Internal *mpInternal;
  };

  // Standard gate-only units
  class BassDrum : public od::Object {
  public: BassDrum(); virtual ~BassDrum();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class SnareDrum : public od::Object {
  public: SnareDrum(); virtual ~SnareDrum();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class HighHat : public od::Object {
  public: HighHat(); virtual ~HighHat();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class FmDrum : public od::Object {
  public: FmDrum(); virtual ~FmDrum();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class BouncingBall : public od::Object {
  public: BouncingBall(); virtual ~BouncingBall();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class NumberStation : public od::Object {
  public: NumberStation(); virtual ~NumberStation();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class RandomisedEnvelope : public od::Object {
  public: RandomisedEnvelope(); virtual ~RandomisedEnvelope();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS
#endif
  private: struct Internal; Internal *mpInternal;
  };

  class ByteBeats : public od::Object {
  public: ByteBeats(); virtual ~ByteBeats();
#ifndef SWIGLUA
  PEAKS_UNIT_MEMBERS
#endif
  private: struct Internal; Internal *mpInternal;
  };

} // namespace peaks_unit
