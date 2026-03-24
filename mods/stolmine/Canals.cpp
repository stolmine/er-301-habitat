// Canals — linked resonant filter (Three Sisters clone)

#include "Canals.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/units.h"

namespace stolmine
{

  static const int kMaxFrameLength = 256;

  struct Canals::Internal
  {
    stmlib::Svf low1, low2;
    stmlib::Svf ctr1, ctr2;
    stmlib::Svf hi1, hi2;

    // Heap-allocated work buffers (avoid stack overflow)
    float lowOut[kMaxFrameLength];
    float ctrOut[kMaxFrameLength];
    float hiOut[kMaxFrameLength];
    float stage1[kMaxFrameLength];

    // Cached params for change detection
    float prevFundamental;
    float prevSpan;
    float prevQuality;
    float prevVoct;
    int prevMode;

    void Init()
    {
      low1.Init(); low2.Init();
      ctr1.Init(); ctr2.Init();
      hi1.Init();  hi2.Init();
      prevFundamental = -999.0f;
      prevSpan = -1.0f;
      prevQuality = -999.0f;
      prevVoct = -999.0f;
      prevMode = -1;
    }
  };

  Canals::Canals()
  {
    addInput(mIn);
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mFundamental);
    addParameter(mSpan);
    addParameter(mQuality);
    addParameter(mOutput);
    addParameter(mMode);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Canals::~Canals()
  {
    delete mpInternal;
  }

  static inline float clampNorm(float hz)
  {
    float f = hz / 48000.0f;
    if (f < 0.001f) f = 0.001f;
    if (f > 0.499f) f = 0.499f;
    return f;
  }

  void Canals::process()
  {
    Internal &s = *mpInternal;

    float *in = mIn.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();

    float fundamental = mFundamental.value();
    float span = mSpan.value();
    if (span < 0.0f) span = 0.0f;
    if (span > 1.0f) span = 1.0f;

    float quality = mQuality.value();
    if (quality < -1.0f) quality = -1.0f;
    if (quality > 1.0f) quality = 1.0f;

    float outputPos = mOutput.value();
    if (outputPos < 0.0f) outputPos = 0.0f;
    if (outputPos > 1.0f) outputPos = 1.0f;

    int mode = (int)(mMode.value() + 0.5f);
    if (mode < 0) mode = 0;
    if (mode > 1) mode = 1;

    float v = voct[0];

    // Reconfigure SVFs only when parameters change
    if (fundamental != s.prevFundamental || span != s.prevSpan ||
        quality != s.prevQuality || mode != s.prevMode ||
        (v > s.prevVoct + 0.001f || v < s.prevVoct - 0.001f))
    {
      float q = 0.5f;
      if (quality >= 0.0f) {
        q = 0.5f + quality * quality * 49.5f;
      }

      float totalSemitones = v * 12.0f * 5.0f + fundamental;
      float freqHz = 261.63f * stmlib::SemitonesToRatio(totalSemitones);
      if (freqHz < 20.0f) freqHz = 20.0f;
      if (freqHz > 20000.0f) freqHz = 20000.0f;

      float spanSemitones = span * 48.0f;
      float lowHz  = freqHz * stmlib::SemitonesToRatio(-spanSemitones);
      float highHz = freqHz * stmlib::SemitonesToRatio(+spanSemitones);
      if (lowHz < 20.0f) lowHz = 20.0f;
      if (highHz > 20000.0f) highHz = 20000.0f;

      float lowF  = clampNorm(lowHz);
      float highF = clampNorm(highHz);
      float ctrF  = clampNorm(freqHz);

      if (mode == 0) {
        s.low1.set_f_q<stmlib::FREQUENCY_DIRTY>(lowF, q);
        s.low2.set_f_q<stmlib::FREQUENCY_DIRTY>(lowF, q);
        s.ctr1.set_f_q<stmlib::FREQUENCY_DIRTY>(lowF, q);
        s.ctr2.set_f_q<stmlib::FREQUENCY_DIRTY>(highF, q);
        s.hi1.set_f_q<stmlib::FREQUENCY_DIRTY>(highF, q);
        s.hi2.set_f_q<stmlib::FREQUENCY_DIRTY>(highF, q);
      } else {
        s.low1.set_f_q<stmlib::FREQUENCY_DIRTY>(lowF, q);
        s.low2.set_f_q<stmlib::FREQUENCY_DIRTY>(lowF, q);
        s.ctr1.set_f_q<stmlib::FREQUENCY_DIRTY>(ctrF, q);
        s.ctr2.set_f_q<stmlib::FREQUENCY_DIRTY>(ctrF, q);
        s.hi1.set_f_q<stmlib::FREQUENCY_DIRTY>(highF, q);
        s.hi2.set_f_q<stmlib::FREQUENCY_DIRTY>(highF, q);
      }

      s.prevFundamental = fundamental;
      s.prevSpan = span;
      s.prevQuality = quality;
      s.prevVoct = v;
      s.prevMode = mode;
    }

    float antiRes = (quality < 0.0f) ? -quality : 0.0f;

    // Process all three filter blocks
    float *lowOut = s.lowOut;
    float *ctrOut = s.ctrOut;
    float *hiOut = s.hiOut;
    float *stage1 = s.stage1;

    if (mode == 0) {
      s.low1.Process<stmlib::FILTER_MODE_LOW_PASS>(in, stage1, FRAMELENGTH);
      s.low2.Process<stmlib::FILTER_MODE_LOW_PASS>(stage1, lowOut, FRAMELENGTH);
      s.ctr1.Process<stmlib::FILTER_MODE_HIGH_PASS>(in, stage1, FRAMELENGTH);
      s.ctr2.Process<stmlib::FILTER_MODE_LOW_PASS>(stage1, ctrOut, FRAMELENGTH);
      s.hi1.Process<stmlib::FILTER_MODE_HIGH_PASS>(in, stage1, FRAMELENGTH);
      s.hi2.Process<stmlib::FILTER_MODE_HIGH_PASS>(stage1, hiOut, FRAMELENGTH);
    } else {
      s.low1.Process<stmlib::FILTER_MODE_LOW_PASS>(in, stage1, FRAMELENGTH);
      s.low2.Process<stmlib::FILTER_MODE_HIGH_PASS>(stage1, lowOut, FRAMELENGTH);
      s.ctr1.Process<stmlib::FILTER_MODE_HIGH_PASS>(in, stage1, FRAMELENGTH);
      s.ctr2.Process<stmlib::FILTER_MODE_LOW_PASS>(stage1, ctrOut, FRAMELENGTH);
      s.hi1.Process<stmlib::FILTER_MODE_LOW_PASS>(in, stage1, FRAMELENGTH);
      s.hi2.Process<stmlib::FILTER_MODE_HIGH_PASS>(stage1, hiOut, FRAMELENGTH);
    }

    // Anti-resonance
    if (antiRes > 0.0f) {
      for (int i = 0; i < FRAMELENGTH; i++) {
        float dry = in[i];
        lowOut[i] = lowOut[i] * (1.0f - antiRes) + (dry - lowOut[i]) * antiRes;
        ctrOut[i] = ctrOut[i] * (1.0f - antiRes) + (dry - ctrOut[i]) * antiRes;
        hiOut[i]  = hiOut[i]  * (1.0f - antiRes) + (dry - hiOut[i])  * antiRes;
      }
    }

    // Output crossfade
    float pos = outputPos * 3.0f;
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float wL, wC, wH;
      if (pos <= 1.0f) {
        wL = 1.0f - pos; wC = pos; wH = 0.0f;
      } else if (pos <= 2.0f) {
        wL = 0.0f; wC = 2.0f - pos; wH = pos - 1.0f;
      } else {
        float t = pos - 2.0f;
        wL = t * 0.333f;
        wC = t * 0.333f;
        wH = (1.0f - t) + t * 0.333f;
      }
      float v = lowOut[i] * wL + ctrOut[i] * wC + hiOut[i] * wH;
      if (v != v || v > 10.0f || v < -10.0f) v = 0.0f;
      out[i] = v;
    }
  }

} // namespace stolmine
