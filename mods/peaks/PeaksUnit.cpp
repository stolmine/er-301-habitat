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

// Parameter conversion: unipolar [0,1] and bipolar [-1,1] to uint16
static inline uint16_t toUni(float v) {
  return static_cast<uint16_t>(CLAMP(0.0f, 1.0f, v) * 65535);
}
static inline uint16_t toBip(float v) {
  return static_cast<uint16_t>((CLAMP(-1.0f, 1.0f, v) * 0.5f + 0.5f) * 65535);
}

// bipMask: bit 0=p1, bit 1=p2, bit 2=p3, bit 3=p4. Set = bipolar.
#define IMPLEMENT_PEAKS_UNIT(UnitName, ProcessorType, bipMask, InitExtra)    \
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
      (bipMask & 1) ? toBip(mParam1.value()) : toUni(mParam1.value()),      \
      (bipMask & 2) ? toBip(mParam2.value()) : toUni(mParam2.value()),      \
      (bipMask & 4) ? toBip(mParam3.value()) : toUni(mParam3.value()),      \
      (bipMask & 8) ? toBip(mParam4.value()) : toUni(mParam4.value())       \
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

  // Peaks units                                    bipolar mask
  IMPLEMENT_PEAKS_UNIT(BassDrum,      peaks::BassDrum,      0x1, )  // p1
  IMPLEMENT_PEAKS_UNIT(SnareDrum,     peaks::SnareDrum,     0x0, )
  IMPLEMENT_PEAKS_UNIT(HighHat,       peaks::HighHat,       0x0, )
  IMPLEMENT_PEAKS_UNIT(FmDrum,        peaks::FmDrum,        0x0, )
  IMPLEMENT_PEAKS_UNIT(BouncingBall,  peaks::BouncingBall,  0x8, )  // p4
  IMPLEMENT_PEAKS_UNIT(MiniSequencer, peaks::MiniSequencer,  0xF, )  // all
  IMPLEMENT_PEAKS_UNIT(NumberStation, peaks::NumberStation,  0x0, )
  IMPLEMENT_PEAKS_UNIT(TapLfo,        peaks::Lfo,           0xC, mpInternal->processor.set_sync(true);) // p3,p4

  // DMC units
  IMPLEMENT_PEAKS_UNIT(RandomisedEnvelope, peaks::RandomisedEnvelope, 0x0, )
  IMPLEMENT_PEAKS_UNIT(ModSequencer,  peaks::ModSequencer,   0xF, )  // all
  IMPLEMENT_PEAKS_UNIT(FmLfo,         peaks::FmLfo,         0xC, )  // p3,p4
  IMPLEMENT_PEAKS_UNIT(WsmLfo,        peaks::WsmLfo,        0xC, )  // p3,p4
  IMPLEMENT_PEAKS_UNIT(Plo,           peaks::Plo,           0x4, )  // p3
  IMPLEMENT_PEAKS_UNIT(ByteBeats,     peaks::ByteBeats,     0x0, )

} // namespace peaks_unit
