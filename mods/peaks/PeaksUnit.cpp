// Peaks / Dead Man's Catch unit wrappers for ER-301
// Based on code by Émilie Gillet and Tim Churches, MIT License

#include "PeaksUnit.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

#include "peaks/gate_processor.h"
#include "peaks/modulations/bouncing_ball.h"
#include "peaks/modulations/lfo.h"
#include "peaks/modulations/mini_sequencer.h"
#include "peaks/modulations/multistage_envelope.h"
#include "peaks/modulations/turing_machine.h"
#include "peaks/drums/bass_drum.h"
#include "peaks/drums/snare_drum.h"
#include "peaks/drums/high_hat.h"
#include "peaks/drums/fm_drum.h"
#include "peaks/number_station/number_station.h"
#include "peaks/number_station/bytebeats.h"

namespace peaks_unit
{

// Internal struct holds the actual Peaks processor + gate state
#define DEFINE_INTERNAL(ProcessorType)                                       \
  struct Internal {                                                          \
    ProcessorType processor;                                                 \
    peaks::GateFlags previousGate;                                           \
  };

// Common constructor/destructor/process implementation
#define IMPLEMENT_PEAKS_UNIT(UnitName, ProcessorType, InitExtra)             \
  struct UnitName::Internal {                                                \
    ProcessorType processor;                                                 \
    peaks::GateFlags previousGate;                                           \
  };                                                                         \
  UnitName::UnitName() {                                                     \
    addInput(mGate);                                                         \
    addOutput(mOut);                                                         \
    addParameter(mParam1);                                                   \
    addParameter(mParam2);                                                   \
    addParameter(mParam3);                                                   \
    addParameter(mParam4);                                                   \
    mpInternal = new Internal();                                             \
    memset(mpInternal, 0, sizeof(Internal));                                 \
    mpInternal->processor.Init();                                            \
    mpInternal->previousGate = peaks::GATE_FLAG_LOW;                         \
    InitExtra                                                                \
  }                                                                          \
  UnitName::~UnitName() { delete mpInternal; }                               \
  void UnitName::process() {                                                 \
    Internal &s = *mpInternal;                                               \
    float *gate = mGate.buffer();                                            \
    float *out = mOut.buffer();                                              \
    uint16_t params[4] = {                                                   \
      static_cast<uint16_t>(CLAMP(0.0f, 1.0f, mParam1.value()) * 65535),    \
      static_cast<uint16_t>(CLAMP(0.0f, 1.0f, mParam2.value()) * 65535),    \
      static_cast<uint16_t>(CLAMP(0.0f, 1.0f, mParam3.value()) * 65535),    \
      static_cast<uint16_t>(CLAMP(0.0f, 1.0f, mParam4.value()) * 65535)     \
    };                                                                       \
    s.processor.Configure(params, peaks::CONTROL_MODE_FULL);                 \
    peaks::GateFlags gateFlags[FRAMELENGTH];                                 \
    for (int i = 0; i < FRAMELENGTH; i++) {                                  \
      s.previousGate = peaks::ExtractGateFlags(s.previousGate,               \
                                               gate[i] > 0.1f);             \
      gateFlags[i] = s.previousGate;                                         \
    }                                                                        \
    int16_t output[FRAMELENGTH];                                             \
    s.processor.Process(gateFlags, output, FRAMELENGTH);                     \
    for (int i = 0; i < FRAMELENGTH; i++) {                                  \
      out[i] = output[i] / 32768.0f;                                        \
    }                                                                        \
  }

  // Peaks units
  IMPLEMENT_PEAKS_UNIT(BassDrum, peaks::BassDrum, )
  IMPLEMENT_PEAKS_UNIT(SnareDrum, peaks::SnareDrum, )
  IMPLEMENT_PEAKS_UNIT(HighHat, peaks::HighHat, )
  IMPLEMENT_PEAKS_UNIT(FmDrum, peaks::FmDrum, )
  IMPLEMENT_PEAKS_UNIT(BouncingBall, peaks::BouncingBall, )
  IMPLEMENT_PEAKS_UNIT(MiniSequencer, peaks::MiniSequencer, )
  IMPLEMENT_PEAKS_UNIT(NumberStation, peaks::NumberStation, )
  IMPLEMENT_PEAKS_UNIT(TapLfo, peaks::Lfo, mpInternal->processor.set_sync(true);)

  // DMC units
  IMPLEMENT_PEAKS_UNIT(RandomisedEnvelope, peaks::RandomisedEnvelope, )
  IMPLEMENT_PEAKS_UNIT(ModSequencer, peaks::ModSequencer, )
  IMPLEMENT_PEAKS_UNIT(FmLfo, peaks::FmLfo, )
  IMPLEMENT_PEAKS_UNIT(WsmLfo, peaks::WsmLfo, )
  IMPLEMENT_PEAKS_UNIT(Plo, peaks::Plo, )
  IMPLEMENT_PEAKS_UNIT(ByteBeats, peaks::ByteBeats, )

} // namespace peaks_unit
