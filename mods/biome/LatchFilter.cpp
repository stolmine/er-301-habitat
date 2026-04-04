// Latch Filter — switched-capacitor style filter (S&H → SVF)
// Ported from monokit SuperCollider implementation

#include "LatchFilter.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/units.h"

namespace stolmine
{

  struct LatchFilter::Internal
  {
    stmlib::Svf svf;
    float latchValue;
    float latchCounter;

    void Init()
    {
      svf.Init();
      latchValue = 0.0f;
      latchCounter = 0.0f;
    }
  };

  LatchFilter::LatchFilter()
  {
    addInput(mIn);
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mFundamental);
    addParameter(mResonance);
    addParameter(mMode);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  LatchFilter::~LatchFilter()
  {
    delete mpInternal;
  }

  void LatchFilter::process()
  {
    Internal &s = *mpInternal;

    float *in = mIn.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();

    float fundamental = mFundamental.value(); // semitones offset
    float resonance = mResonance.value();
    if (resonance < 0.0f) resonance = 0.0f;
    if (resonance > 1.0f) resonance = 1.0f;

    int mode = (int)(mMode.value() + 0.5f);
    if (mode < 0) mode = 0;
    if (mode > 1) mode = 1;

    // Q from 0.5 (low res) to 20 (high res)
    float q = 0.5f + resonance * 19.5f;

    // Process per-sample for V/Oct tracking
    float latchBuf[FRAMELENGTH];
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Cutoff from V/Oct + fundamental offset (semitones)
      // ER-301 fullscale 10V: signal 0.1 = 1V = 12 semitones
      float totalSemitones = voct[i] * 120.0f + fundamental;
      // MIDI note 60 = middle C = 261.63 Hz
      float cutoff = 261.63f * stmlib::SemitonesToRatio(totalSemitones);
      if (cutoff < 20.0f) cutoff = 20.0f;
      if (cutoff > 20000.0f) cutoff = 20000.0f;

      // S&H: latch input at cutoff/8 rate
      float latchFreq = cutoff / 8.0f;
      if (latchFreq < 100.0f) latchFreq = 100.0f;
      if (latchFreq > 20000.0f) latchFreq = 20000.0f;
      float latchPeriod = 48000.0f / latchFreq;

      s.latchCounter += 1.0f;
      if (s.latchCounter >= latchPeriod) {
        s.latchCounter -= latchPeriod;
        s.latchValue = in[i];
      }
      latchBuf[i] = s.latchValue;

      // Update SVF cutoff per sample for smooth tracking
      float normalizedCutoff = cutoff / 48000.0f;
      if (normalizedCutoff > 0.499f) normalizedCutoff = 0.499f;
      s.svf.set_f_q<stmlib::FREQUENCY_FAST>(normalizedCutoff, q);
    }

    if (mode == 0) {
      s.svf.Process<stmlib::FILTER_MODE_LOW_PASS>(latchBuf, out, FRAMELENGTH);
    } else {
      s.svf.Process<stmlib::FILTER_MODE_HIGH_PASS>(latchBuf, out, FRAMELENGTH);
    }
  }

} // namespace stolmine
