// Canals - linked resonant filter (Three Sisters inspired)
// Custom SVF core with tanh-saturating integrators

#include "Canals.h"
#include "SistersSvf.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

#include "stmlib/dsp/units.h"

namespace stolmine
{

  static const int kMaxFrameLength = 256;

  struct Canals::Internal
  {
    SistersSvf low1, low2;
    SistersSvf ctr1, ctr2;
    SistersSvf hi1, hi2;

    float lowOut[kMaxFrameLength];
    float ctrOut[kMaxFrameLength];
    float hiOut[kMaxFrameLength];

    float prevFundamental;
    float prevSpan;
    float prevQuality;
    float prevVoct;
    int prevMode;

    void Init()
    {
      low1.reset(); low2.reset();
      ctr1.reset(); ctr2.reset();
      hi1.reset();  hi2.reset();
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
    float span = CLAMP(0.0f, 1.0f, mSpan.value());
    float quality = CLAMP(-1.0f, 1.0f, mQuality.value());
    float outputPos = CLAMP(0.0f, 3.0f, mOutput.value());
    int mode = CLAMP(0, 1, (int)(mMode.value() + 0.5f));

    float v = voct[0];

    // Reconfigure SVFs when parameters change
    if (fundamental != s.prevFundamental || span != s.prevSpan ||
        quality != s.prevQuality || mode != s.prevMode ||
        (v > s.prevVoct + 0.001f || v < s.prevVoct - 0.001f))
    {
      // Q mapping: 0.5 at quality=0, up to 100 at quality=1
      // Cubic curve for gentle low end, aggressive high end
      float q = 0.5f;
      if (quality >= 0.0f)
      {
        float t = quality * quality * quality;
        q = 0.5f + t * 99.5f;
      }

      float totalSemitones = v * 12.0f * 5.0f + fundamental;
      float freqHz = 261.63f * stmlib::SemitonesToRatio(totalSemitones);
      freqHz = CLAMP(20.0f, 20000.0f, freqHz);

      float spanSemitones = span * 48.0f;
      float lowHz = CLAMP(20.0f, 20000.0f,
                          freqHz * stmlib::SemitonesToRatio(-spanSemitones));
      float highHz = CLAMP(20.0f, 20000.0f,
                           freqHz * stmlib::SemitonesToRatio(+spanSemitones));

      float lowF = clampNorm(lowHz);
      float highF = clampNorm(highHz);
      float ctrF = clampNorm(freqHz);

      if (mode == 0)
      {
        // Crossover: LOW at lowF, CENTRE spans lowF->highF, HIGH at highF
        s.low1.setFreqQ(lowF, q);
        s.low2.setFreqQ(lowF, q);
        s.ctr1.setFreqQ(lowF, q);
        s.ctr2.setFreqQ(highF, q);
        s.hi1.setFreqQ(highF, q);
        s.hi2.setFreqQ(highF, q);
      }
      else
      {
        // Formant: each at its own frequency
        s.low1.setFreqQ(lowF, q);
        s.low2.setFreqQ(lowF, q);
        s.ctr1.setFreqQ(ctrF, q);
        s.ctr2.setFreqQ(ctrF, q);
        s.hi1.setFreqQ(highF, q);
        s.hi2.setFreqQ(highF, q);
      }

      s.prevFundamental = fundamental;
      s.prevSpan = span;
      s.prevQuality = quality;
      s.prevVoct = v;
      s.prevMode = mode;
    }

    float antiRes = (quality < 0.0f) ? -quality : 0.0f;

    float *lowOut = s.lowOut;
    float *ctrOut = s.ctrOut;
    float *hiOut = s.hiOut;

    // Per-sample processing with cascaded SVFs
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i];

      if (mode == 0)
      {
        // Crossover: LP->LP, HP->LP, HP->HP
        auto lo1 = s.low1.process(x);
        auto lo2 = s.low2.process(lo1.lp);

        auto ct1 = s.ctr1.process(x);
        auto ct2 = s.ctr2.process(ct1.hp);

        auto hi1 = s.hi1.process(x);
        auto hi2 = s.hi2.process(hi1.hp);

        lowOut[i] = lo2.lp;
        ctrOut[i] = ct2.lp;
        hiOut[i] = hi2.hp;
      }
      else
      {
        // Formant: all bandpass (LP->HP, HP->LP, LP->HP)
        auto lo1 = s.low1.process(x);
        auto lo2 = s.low2.process(lo1.lp);

        auto ct1 = s.ctr1.process(x);
        auto ct2 = s.ctr2.process(ct1.hp);

        auto hi1 = s.hi1.process(x);
        auto hi2 = s.hi2.process(hi1.lp);

        lowOut[i] = lo2.hp;
        ctrOut[i] = ct2.lp;
        hiOut[i] = hi2.hp;
      }
    }

    // Anti-resonance: notch by mixing complementary signal
    if (antiRes > 0.0f)
    {
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        float dry = in[i];
        lowOut[i] = lowOut[i] * (1.0f - antiRes) + (dry - lowOut[i]) * antiRes;
        ctrOut[i] = ctrOut[i] * (1.0f - antiRes) + (dry - ctrOut[i]) * antiRes;
        hiOut[i] = hiOut[i] * (1.0f - antiRes) + (dry - hiOut[i]) * antiRes;
      }
    }

    // Output crossfade: LOW -> CENTRE -> HIGH -> ALL
    float pos = outputPos;
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float wL, wC, wH;
      if (pos <= 1.0f)
      {
        wL = 1.0f - pos;
        wC = pos;
        wH = 0.0f;
      }
      else if (pos <= 2.0f)
      {
        wL = 0.0f;
        wC = 2.0f - pos;
        wH = pos - 1.0f;
      }
      else
      {
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
