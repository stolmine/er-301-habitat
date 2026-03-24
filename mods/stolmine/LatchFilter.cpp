// Latch Filter — switched-capacitor style filter (S&H → SVF)
// Ported from monokit SuperCollider implementation

#include "LatchFilter.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

#include "stmlib/dsp/filter.h"

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
    addOutput(mOut);
    addParameter(mCutoff);
    addParameter(mResonance);
    addOption(mMode);

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
    float *out = mOut.buffer();

    float cutoff = mCutoff.value();
    if (cutoff < 20.0f) cutoff = 20.0f;
    if (cutoff > 20000.0f) cutoff = 20000.0f;

    float resonance = mResonance.value();
    if (resonance < 0.0f) resonance = 0.0f;
    if (resonance > 1.0f) resonance = 1.0f;

    int mode = mMode.value();

    // S&H rate is cutoff / 8, clamped to 100-20000 Hz
    float latchFreq = cutoff / 8.0f;
    if (latchFreq < 100.0f) latchFreq = 100.0f;
    if (latchFreq > 20000.0f) latchFreq = 20000.0f;
    float latchPeriod = 48000.0f / latchFreq;

    // SVF setup: Q from 0.5 (low res) to 20 (high res)
    float q = 0.5f + resonance * 19.5f;
    float normalizedCutoff = cutoff / 48000.0f;
    if (normalizedCutoff > 0.499f) normalizedCutoff = 0.499f;
    s.svf.set_f_q<stmlib::FREQUENCY_FAST>(normalizedCutoff, q);

    // Process: S&H then filter
    float latchBuf[FRAMELENGTH];
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      s.latchCounter += 1.0f;
      if (s.latchCounter >= latchPeriod) {
        s.latchCounter -= latchPeriod;
        s.latchValue = in[i];
      }
      latchBuf[i] = s.latchValue;
    }

    if (mode == 0) {
      s.svf.Process<stmlib::FILTER_MODE_LOW_PASS>(latchBuf, out, FRAMELENGTH);
    } else {
      s.svf.Process<stmlib::FILTER_MODE_HIGH_PASS>(latchBuf, out, FRAMELENGTH);
    }
  }

} // namespace stolmine
